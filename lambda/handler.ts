import { exec } from "child_process";
import { promisify } from "util";

const execAsync = promisify(exec);

export const handler = async () => {
  // Execute: "lotio --version" and "ffmpeg -version"
  const lotioVersion = (await execAsync("lotio --version")).stdout.trim();
  const ffmpegVersion = (await execAsync("ffmpeg -version")).stdout.trim();

  return {
    statusCode: 200,
    body: JSON.stringify({ lotioVersion, ffmpegVersion }),
  };
};

