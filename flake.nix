{
  description = "Iris — universal runtime type bus. PoC: core IR + JVM backend.";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll  = f: nixpkgs.lib.genAttrs systems (system: f system (import nixpkgs { inherit system; }));
      version = "0.1.0-poc";
    in
    {
      packages = forAll (system: pkgs:
        let
          stdenv = pkgs.gcc16Stdenv;
          jdk    = pkgs.openjdk21;

          iris = stdenv.mkDerivation {
            pname = "iris"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ jdk pkgs.gtest ];
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release" ];
            installPhase      = "cmake --install . --prefix $out";
            meta.description  = "Iris core library — runtime type bus with JVM backend.";
            meta.platforms    = pkgs.lib.platforms.linux;
          };

          iris-tests = stdenv.mkDerivation {
            pname = "iris-tests"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ jdk pkgs.gtest iris ];
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Debug" ];
            doCheck           = true;
            checkPhase        = "export JAVA_HOME=${jdk}; ctest --output-on-failure -j$(nproc)";
            installPhase      = "mkdir -p $out/bin; cp iris_tests $out/bin/";
          };
        in
        { default = iris; inherit iris iris-tests; }
      );

      devShells = forAll (system: pkgs:
        let stdenv = pkgs.gcc16Stdenv; jdk = pkgs.openjdk21; in
        {
          default = pkgs.mkShell.override { inherit stdenv; } {
            name     = "iris-dev";
            packages = [ pkgs.cmake pkgs.ninja pkgs.gtest pkgs.pkg-config jdk pkgs.clang-tools pkgs.gdb pkgs.valgrind ];
            shellHook = ''
              export JAVA_HOME=${jdk}
              export CC=${stdenv.cc}/bin/gcc
              export CXX=${stdenv.cc}/bin/g++
              echo "iris dev — GCC $(g++ --version | head -1 | grep -oP '\d+\.\d+\.\d+'), JDK $JAVA_HOME"
            '';
          };
        }
      );
    };
}
