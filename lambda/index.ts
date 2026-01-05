import { S3Client, GetObjectCommand, PutObjectCommand } from '@aws-sdk/client-s3';
import { Upload } from '@aws-sdk/lib-storage';
import { spawn } from 'child_process';
import * as fs from 'fs/promises';
import { createReadStream, createWriteStream } from 'fs';
import * as path from 'path';
import * as os from 'os';
import { Readable } from 'stream';
import { pipeline } from 'stream/promises';
const s3 = new S3Client({});

interface LambdaEvent {
  jsonUrl: string; // HTTP or S3 URL to Lottie JSON file
  fps?: number; // Optional FPS (default: 30)
  textConfigUrl?: string; // Optional HTTP or S3 URL to text configuration JSON file
  outputS3Bucket: string;
  outputS3Key: string;
}

interface LottieAsset {
  id?: string;
  u?: string; // Base path (e.g., "images/" or "https://..." or "s3://...")
  p?: string; // Filename
  [key: string]: any;
}

interface LottieFont {
  fPath?: string; // Font file path (can be HTTP/S3 URI)
  fName?: string;
  fFamily?: string;
  [key: string]: any;
}

interface LottieJson {
  assets?: LottieAsset[];
  fonts?: {
    list?: LottieFont[];
  };
  [key: string]: any;
}

/**
 * Parse S3 URI (s3://bucket/key) into bucket and key
 */
function parseS3Uri(s3Uri: string): { bucket: string; key: string } {
  const match = s3Uri.match(/^s3:\/\/([^/]+)\/(.+)$/);
  if (!match) {
    throw new Error(`Invalid S3 URI: ${s3Uri}`);
  }
  return { bucket: match[1], key: match[2] };
}

/**
 * Download file from S3 to local path (streaming)
 */
async function downloadFromS3(
  s3Uri: string,
  localPath: string
): Promise<string> {
  const { bucket, key } = parseS3Uri(s3Uri);
  
  console.log(`[DOWNLOAD] Downloading s3://${bucket}/${key} to ${localPath}`);
  
  const command = new GetObjectCommand({ Bucket: bucket, Key: key });
  const response = await s3.send(command);
  
  if (!response.Body) {
    throw new Error(`Empty response for ${s3Uri}`);
  }
  
  // Ensure directory exists
  await fs.mkdir(path.dirname(localPath), { recursive: true });
  
  // Stream directly to file instead of buffering in memory
  const stream = response.Body as Readable;
  const writeStream = createWriteStream(localPath);
  
  await pipeline(stream, writeStream);
  
  const fileSize = (await fs.stat(localPath)).size;
  console.log(`[DOWNLOAD] Downloaded ${s3Uri} (${fileSize} bytes)`);
  
  return localPath;
}

/**
 * Download file from HTTP URL to local path (streaming)
 */
async function downloadFromHttp(
  httpUrl: string,
  localPath: string
): Promise<string> {
  console.log(`[DOWNLOAD] Downloading ${httpUrl} to ${localPath}`);
  
  const response = await fetch(httpUrl);
  if (!response.ok) {
    throw new Error(`HTTP error! status: ${response.status} for ${httpUrl}`);
  }
  
  // Ensure directory exists
  await fs.mkdir(path.dirname(localPath), { recursive: true });
  
  // Stream directly to file
  if (!response.body) {
    throw new Error(`Empty response body for ${httpUrl}`);
  }
  
  const writeStream = createWriteStream(localPath);
  const reader = response.body.getReader();
  
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (!writeStream.write(value)) {
        // Wait for drain if buffer is full
        await new Promise<void>(resolve => writeStream.once('drain', () => resolve()));
      }
    }
    writeStream.end();
    
    // Wait for write stream to finish
    await new Promise<void>((resolve, reject) => {
      writeStream.on('finish', resolve);
      writeStream.on('error', reject);
    });
    
    const fileSize = (await fs.stat(localPath)).size;
    console.log(`[DOWNLOAD] Downloaded ${httpUrl} (${fileSize} bytes)`);
    
    return localPath;
  } catch (error) {
    writeStream.destroy();
    throw error;
  }
}

/**
 * Download file from HTTP or S3 URL
 */
async function downloadFromUrl(
  url: string,
  localPath: string
): Promise<string> {
  if (url.startsWith('s3://')) {
    return downloadFromS3(url, localPath);
  } else if (url.startsWith('https://') || url.startsWith('http://')) {
    return downloadFromHttp(url, localPath);
  } else {
    throw new Error(`Unsupported URL scheme: ${url}`);
  }
}

/**
 * Upload file to S3 (streaming version for large files)
 */
async function uploadToS3(
  localPath: string,
  bucket: string,
  key: string
): Promise<void> {
  const stats = await fs.stat(localPath);
  const fileSize = stats.size;
  
  // Always use multipart upload for files > 5MB (streaming)
  if (fileSize > 5 * 1024 * 1024) {
    const fileStream = createReadStream(localPath);
    
    const upload = new Upload({
      client: s3,
      params: {
        Bucket: bucket,
        Key: key,
        Body: fileStream,  // Stream instead of buffer
        ContentType: 'video/quicktime',
      },
      partSize: 10 * 1024 * 1024, // 10MB parts
      queueSize: 4, // Parallel parts
    });
    
    await upload.done();
  } else {
    // For small files, still read into memory (acceptable)
    const fileContent = await fs.readFile(localPath);
    const command = new PutObjectCommand({
      Bucket: bucket,
      Key: key,
      Body: fileContent,
      ContentType: 'video/quicktime',
    });
    await s3.send(command);
  }
  
  console.log(`[UPLOAD] Uploaded ${fileSize} bytes to s3://${bucket}/${key}`);
}

export const handler = async (event: LambdaEvent): Promise<{
  statusCode: number;
  body: string;
  outputS3Key?: string;
}> => {
  const invocationStart = Date.now();
  const tmpdir = await fs.mkdtemp(path.join(os.tmpdir(), 'render-'));
  
  console.log(`[SETUP] Starting render in ${tmpdir}`);
  console.log(`[SETUP] Invocation started at ${new Date().toISOString()}`);
  console.log(`[SETUP] Event:`, JSON.stringify(event, null, 2));
  
  try {
    // 1. Download JSON from URL
    const jsonDownloadStart = Date.now();
    let lottieJson: LottieJson;
    
    console.log(`[DOWNLOAD] Downloading JSON from ${event.jsonUrl}`);
    const jsonPath = path.join(tmpdir, 'input.json');
    await downloadFromUrl(event.jsonUrl, jsonPath);
    
    const jsonDownloadTime = Date.now() - jsonDownloadStart;
    console.log(`[TIMING] JSON download: ${jsonDownloadTime}ms`);
    
    // 2. Parse JSON
    const parseStart = Date.now();
    try {
      const jsonContent = await fs.readFile(jsonPath, 'utf-8');
      lottieJson = JSON.parse(jsonContent);
    } catch (error: any) {
      throw new Error(`Failed to parse JSON: ${error.message}`);
    }
    
    const parseTime = Date.now() - parseStart;
    console.log(`[TIMING] JSON parsing: ${parseTime}ms`);
    
    // 3. Collect all download tasks for parallel execution
    const downloadTasks: Promise<void>[] = [];
    
    // 4. Process assets - download from HTTP/S3 if needed
    const assetsDir = path.join(tmpdir, 'images');
    await fs.mkdir(assetsDir, { recursive: true });
    
    let assetCount = 0;
    for (const asset of lottieJson.assets || []) {
      if (asset.u && (asset.u.startsWith('https://') || asset.u.startsWith('http://') || asset.u.startsWith('s3://'))) {
        assetCount++;
        const filename = asset.p || 'asset';
        const localPath = path.join(assetsDir, filename);
        
        // Build full URL
        let fullUrl = asset.u;
        if (asset.p && !asset.u.endsWith('/') && !asset.u.endsWith(asset.p)) {
          fullUrl = asset.u.endsWith('/') ? `${asset.u}${asset.p}` : `${asset.u}/${asset.p}`;
        } else if (asset.p && asset.u.endsWith('/')) {
          fullUrl = `${asset.u}${asset.p}`;
        }
        
        downloadTasks.push(
          downloadFromUrl(fullUrl, localPath).then(() => {
            // Update asset path to relative local path
            asset.u = 'images/';
            asset.p = filename;
          })
        );
      }
    }
    
    // 5. Process fonts - download from HTTP/S3 if needed
    const fontsDir = path.join(tmpdir, 'fonts');
    await fs.mkdir(fontsDir, { recursive: true });
    
    let fontCount = 0;
    for (const font of lottieJson.fonts?.list || []) {
      if (font.fPath && (font.fPath.startsWith('https://') || font.fPath.startsWith('http://') || font.fPath.startsWith('s3://'))) {
        fontCount++;
        const fontFilename = path.basename(font.fPath) || `${font.fName || 'font'}.ttf`;
        const localPath = path.join(fontsDir, fontFilename);
        
        downloadTasks.push(
          downloadFromUrl(font.fPath, localPath).then(() => {
            // Update font path to local absolute path
            font.fPath = localPath;
          })
        );
      }
    }
    
    console.log(`[INFO] Found ${assetCount} assets and ${fontCount} fonts to download`);
    
    // 6. Download all assets and fonts in parallel
    const downloadStart = Date.now();
    if (downloadTasks.length > 0) {
      console.log(`[DOWNLOAD] Downloading ${downloadTasks.length} files in parallel...`);
      await Promise.all(downloadTasks);
    }
    const downloadTime = Date.now() - downloadStart;
    console.log(`[TIMING] Assets/fonts download (parallel): ${downloadTime}ms`);
    
    // 7. Update JSON paths
    const pathReplaceStart = Date.now();
    // Paths already updated during download, just need to save
    const pathReplaceTime = Date.now() - pathReplaceStart;
    console.log(`[TIMING] Path replacement: ${pathReplaceTime}ms`);
    
    // 8. Save modified JSON to temp directory
    const jsonOutputPath = path.join(tmpdir, 'animation.json');
    await fs.writeFile(jsonOutputPath, JSON.stringify(lottieJson));
    
    // 9. Download text configuration file if provided
    let textConfigPath: string | undefined;
    if (event.textConfigUrl) {
      const textConfigUrl = event.textConfigUrl; // Type narrowing
      console.log(`[DOWNLOAD] Downloading text configuration from ${textConfigUrl}`);
      textConfigPath = path.join(tmpdir, 'text-config.json');
      await downloadFromUrl(textConfigUrl, textConfigPath);
      console.log(`[DOWNLOAD] Text configuration downloaded to ${textConfigPath}`);
    }
    
    // 10. Render frames and encode to MOV in one streaming pipeline
    const fps = event.fps || 30;
    const outputMov = path.join(tmpdir, 'output.mov');
    
    console.log(`[RENDER] Rendering frames and encoding MOV (streaming mode): ${jsonOutputPath} -> ${outputMov} (${fps} fps)`);
    if (textConfigPath) {
      console.log(`[RENDER] Using text configuration: ${textConfigPath}`);
    }
    const renderStart = Date.now();
    
    // Build lotio command arguments (streaming mode)
    const renderArgs: string[] = ['--stream'];
    if (textConfigPath) {
      renderArgs.push('--text-config', textConfigPath);
    }
    renderArgs.push(jsonOutputPath, '-', String(fps)); // '-' means stdout
    
    // Build ffmpeg command arguments (reads from stdin via image2pipe)
    const ffmpegArgs = [
      '-y',
      '-f', 'image2pipe',
      '-vcodec', 'png',
      '-framerate', String(fps),
      '-loglevel', 'error',
      '-threads', '0',
      '-thread_type', 'frame',
      '-fflags', '+genpts',
      '-i', 'pipe:0',
      '-vf', 'format=yuva444p10le',
      '-c:v', 'prores_ks',
      '-profile:v', '4444',
      '-pix_fmt', 'yuva444p10le',
      '-slices', '8',
      '-threads', '0',
      '-thread_type', 'frame',
      '-qscale:v', '11',
      '-r', String(fps),
      '-f', 'mov',
      '-movflags', '+faststart',
      outputMov
    ];
    
    // Execute lotio and pipe stdout to ffmpeg stdin
    const renderProcess = spawn('/opt/bin/lotio', renderArgs, {
      env: {
        ...process.env,
        OMP_NUM_THREADS: '6',
        SKIA_THREAD_COUNT: '6'
      },
      stdio: ['ignore', 'pipe', 'pipe'] // stdin: ignore, stdout: pipe (PNG data), stderr: pipe (logs)
    });
    
    const ffmpegProcess = spawn('/opt/ffmpeg/bin/ffmpeg', ffmpegArgs, {
      stdio: ['pipe', 'pipe', 'pipe'] // stdin: pipe (PNG data), stdout: pipe, stderr: pipe
    });
    
    // Pipe lotio stdout (PNG data) to ffmpeg stdin
    renderProcess.stdout.pipe(ffmpegProcess.stdin);
    
    // Collect stderr from both processes for logging
    let renderStderr = '';
    let ffmpegStderr = '';
    
    renderProcess.stderr.on('data', (data: Buffer) => {
      const text = data.toString();
      renderStderr += text;
      // Log render progress/errors in real-time
      if (text.includes('error') || text.includes('Error') || text.includes('ERROR')) {
        console.error(`[RENDER] ${text.trim()}`);
      }
    });
    
    ffmpegProcess.stderr.on('data', (data: Buffer) => {
      const text = data.toString();
      ffmpegStderr += text;
      // Only log actual errors from ffmpeg
      if (text.includes('error') || text.includes('Error') || text.includes('ERROR')) {
        console.error(`[STITCH] ${text.trim()}`);
      }
    });
    
    // Wait for both processes to complete
    await new Promise<void>((resolve, reject) => {
      let renderExited = false;
      let ffmpegExited = false;
      
      const checkComplete = () => {
        if (renderExited && ffmpegExited) {
          resolve();
        }
      };
      
      renderProcess.on('close', (code: number | null) => {
        renderExited = true;
        if (code !== 0) {
          reject(new Error(`lotio failed with exit code ${code}\n${renderStderr}`));
        } else {
          // Close ffmpeg stdin when renderer is done
          ffmpegProcess.stdin.end();
          checkComplete();
        }
      });
      
      ffmpegProcess.on('close', (code: number | null) => {
        ffmpegExited = true;
        if (code !== 0) {
          reject(new Error(`ffmpeg failed with exit code ${code}\n${ffmpegStderr}`));
        } else {
          checkComplete();
        }
      });
      
      renderProcess.on('error', (error: Error) => {
        reject(new Error(`lotio spawn error: ${error.message}`));
      });
      
      ffmpegProcess.on('error', (error: Error) => {
        reject(new Error(`ffmpeg spawn error: ${error.message}`));
      });
    });
    
    const renderTime = Date.now() - renderStart;
    console.log(`[TIMING] Rendering and encoding (streaming): ${renderTime}ms`);
    
    // Extract frame count from render logs if available
    let frameCount: number | undefined;
    const frameCountMatch = renderStderr.match(/Successfully streamed (\d+) frames/);
    if (frameCountMatch) {
      frameCount = parseInt(frameCountMatch[1], 10);
      console.log(`[INFO] Rendered ${frameCount} frames`);
    }
    
    // 11. Verify output file exists
    try {
      const stats = await fs.stat(outputMov);
      console.log(`[INFO] MOV file size: ${stats.size} bytes`);
      if (stats.size === 0) {
        throw new Error('Output MOV file is empty');
      }
    } catch (error: any) {
      throw new Error(`Output MOV file not found or invalid: ${error.message}`);
    }
    
    // 12. Upload to S3
    console.log(`[UPLOAD] Uploading to s3://${event.outputS3Bucket}/${event.outputS3Key}`);
    const uploadStart = Date.now();
    await uploadToS3(outputMov, event.outputS3Bucket, event.outputS3Key);
    const uploadTime = Date.now() - uploadStart;
    console.log(`[TIMING] S3 upload: ${uploadTime}ms`);
    
    // 13. Calculate total time
    const totalTime = Date.now() - invocationStart;
    console.log(`[TIMING] Total invocation time: ${totalTime}ms`);
    
    // Get file size for response
    const movStats = await fs.stat(outputMov);
    
    // Build S3 URL
    const s3Url = `s3://${event.outputS3Bucket}/${event.outputS3Key}`;
    
    return {
      statusCode: 200,
      body: JSON.stringify({
        message: 'Render completed successfully',
        outputS3Url: s3Url,
        outputS3Key: event.outputS3Key,
        outputS3Bucket: event.outputS3Bucket,
        timings: {
          totalInvocationMs: totalTime,
          jsonDownloadMs: jsonDownloadTime,
          jsonParsingMs: parseTime,
          assetsDownloadMs: downloadTime,
          pathReplacementMs: pathReplaceTime,
          renderingAndEncodingMs: renderTime,
          s3UploadMs: uploadTime,
        },
        metadata: {
          frameCount: frameCount,
          movFileSizeBytes: movStats.size,
        }
      }),
      outputS3Key: event.outputS3Key,
    };
    
  } catch (error: any) {
    console.error('[ERROR] Error:', error);
    console.error('[ERROR] Stack:', error.stack);
    
    return {
      statusCode: 500,
      body: JSON.stringify({
        error: error.message || 'Unknown error',
        stack: error.stack,
      }),
    };
  } finally {
    // Clean up temp directory
    try {
      await fs.rm(tmpdir, { recursive: true, force: true });
      console.log(`[CLEANUP] Removed temp directory: ${tmpdir}`);
    } catch (error) {
      console.warn(`[WARN] Failed to clean up ${tmpdir}:`, error);
    }
  }
};

