class ShadowLogis < Formula
  desc "Native freight shipment and shipping document workspace"
  homepage "https://github.com/Milhound/shadow-logis"
  url "https://github.com/Milhound/shadow-logis/releases/download/v0.12.0/shadow-logis-0.12.0-source.zip"
  sha256 "250a0896a46aea7aa43c0d6ee580ba27106024ad60bc673281e36c920a2d9acc"
  license "AGPL-3.0-only"
  head "https://github.com/Milhound/shadow-logis.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on arch: :arm64
  depends_on :macos
  depends_on "qtbase"
  depends_on "qtsvg"
  depends_on "qtwebengine"

  def install
    args = std_cmake_args(install_prefix: libexec) + %W[
      -DBUILD_TESTING=OFF
      -DSHADOW_LOGIS_DEPLOY_RUNTIME=OFF
      -DQt6_DIR=#{Formula["qtbase"].opt_lib}/cmake/Qt6
      -DQt6Pdf_DIR=#{Formula["qtwebengine"].opt_lib}/cmake/Qt6Pdf
      -DQt6PdfWidgets_DIR=#{Formula["qtwebengine"].opt_lib}/cmake/Qt6PdfWidgets
      -DQt6Svg_DIR=#{Formula["qtsvg"].opt_lib}/cmake/Qt6Svg
    ]
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja", *args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"

    # Homebrew owns the shared Qt runtimes; do not copy them into this app.
    (libexec/"shadow-logis.app/Contents/Resources/qt.conf").write <<~EOS
      [Paths]
      Plugins = #{Formula["qtbase"].opt_share}/qt/plugins
    EOS
    bin.install_symlink libexec/"shadow-logis.app/Contents/MacOS/shadow-logis"
  end

  def caveats
    <<~EOS
      Launch Shadow Logis with:
        open #{opt_libexec}/shadow-logis.app

      This source build uses Homebrew's Qt libraries. Keep the app in its
      installed location; it is not a standalone bundle for copying elsewhere.
    EOS
  end

  test do
    workspace = testpath/"test.sqlite"
    system bin/"shadow-logis", "--create-workspace", workspace
    assert_equal "SQLite format 3\0", workspace.binread(16)
    assert_equal 0, workspace.stat.mode & 0o077
    ENV["QT_QPA_PLATFORM"] = "offscreen"
    assert_match "PASS: application runtime", shell_output("#{bin}/shadow-logis --self-test")
  end
end
