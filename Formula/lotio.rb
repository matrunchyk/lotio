class Lotio < Formula
  desc "High-performance Lottie animation frame renderer using Skia"
  homepage "https://github.com/matrunchyk/lotio"  # Update with your actual repo URL
  url "https://github.com/matrunchyk/lotio/archive/refs/tags/v20251228-9496855.tar.gz"
  sha256 "e7319e1815b3c84ca10b0de26c85545baec8986e45b4f14e214850a6d2c62752"
  license "MIT"  # Update with your license

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "python@3.11" => :build
  depends_on "git" => :build
  depends_on "fontconfig"
  depends_on "freetype"
  depends_on "icu4c"
  depends_on "libpng"
  depends_on "jpeg-turbo"
  depends_on "webp"
  depends_on "harfbuzz"

  def install
    # Fetch Skia first (not included in source archive)
    mkdir_p "third_party/skia"
    cd "third_party/skia" do
      system "git", "clone", "--depth", "1", "https://skia.googlesource.com/skia.git"
      cd "skia" do
        system "python3", "tools/git-sync-deps"
      end
    end
    
    # Build Skia
    cd "third_party/skia/skia" do
      system "python3", "bin/fetch-gn"
      
      # Configure GN args for macOS
      gn_args = [
        "target_cpu=\"#{Hardware::CPU.arch == "arm64" ? "arm64" : "x64"}\"",
        "is_official_build=true",
        "is_debug=false",
        "skia_enable_skottie=true",
        "skia_enable_fontmgr_fontconfig=true",
        "skia_enable_fontmgr_custom_directory=true",
        "skia_use_freetype=true",
        "skia_use_libpng_encode=true",
        "skia_use_libpng_decode=true",
        "skia_use_libwebp_decode=true",
        "skia_use_wuffs=true",
        "skia_enable_pdf=false"
      ]
      
      # Add Homebrew include paths for macOS
      if OS.mac?
        homebrew_prefix = HOMEBREW_PREFIX
        freetype_include = "#{Formula["freetype"].opt_include}/freetype2"
        icu_include = "#{Formula["icu4c"].opt_include}"
        harfbuzz_include = "#{Formula["harfbuzz"].opt_include}/harfbuzz"
        
        gn_args << "extra_cflags=[\"-O3\", \"-march=native\", \"-I#{homebrew_prefix}/include\", \"-I#{freetype_include}\", \"-I#{icu_include}\", \"-I#{harfbuzz_include}\"]"
        gn_args << "extra_asmflags=[\"-I#{homebrew_prefix}/include\", \"-I#{freetype_include}\", \"-I#{icu_include}\", \"-I#{harfbuzz_include}\"]"
      end
      
      system "bin/gn", "gen", "out/Release", "--args=#{gn_args.join(' ')}"
      system "ninja", "-C", "out/Release"
    end

    # Build lotio
    system "./build_local.sh"
    
    # Install binary
    bin.install "lotio"
    
    # Install headers (for library distribution)
    include.install Dir["src/core/*.h"] => "lotio/core"
    include.install Dir["src/text/*.h"] => "lotio/text"
    include.install Dir["src/utils/*.h"] => "lotio/utils"
    
    # Install Skia static libraries (for programmatic use)
    skia_lib_dir = "third_party/skia/skia/out/Release"
    %w[skottie skia skparagraph sksg skshaper skunicode_icu skunicode_core skresources jsonreader].each do |lib_name|
      lib_file = "#{skia_lib_dir}/lib#{lib_name}.a"
      lib.install lib_file if File.exist?(lib_file)
    end
    
    # Create and install pkg-config file
    (lib/"pkgconfig").mkpath
    pkgconfig_content = <<~EOF
      prefix=#{HOMEBREW_PREFIX}
      exec_prefix=${prefix}
      libdir=${exec_prefix}/lib
      includedir=${prefix}/include
      
      Name: lotio
      Description: High-performance Lottie animation frame renderer using Skia
      Version: #{version}
      Libs: -L${libdir} -lskottie -lskia -lskparagraph -lsksg -lskshaper -lskunicode_icu -lskunicode_core -lskresources -ljsonreader
      Cflags: -I${includedir}
    EOF
    (lib/"pkgconfig"/"lotio.pc").write(pkgconfig_content)
  end

  test do
    # Test that the binary works
    system "#{bin}/lotio", "--help"
  end
end

