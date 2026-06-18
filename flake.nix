{
  description = "Iris — universal runtime type bus. PoC: core IR + JVM backend.";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.stdexec      = { url = "github:NVIDIA/stdexec"; flake = false; };

  outputs = { self, nixpkgs, stdexec }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll  = f: nixpkgs.lib.genAttrs systems (system: f system (import nixpkgs { inherit system; }));
      version = "0.1.0-poc";

      perSystem = system: pkgs:
        let
          stdenv = pkgs.gcc16Stdenv;
          jdk    = pkgs.openjdk21;

          # stdexec is header-only — skip its CMake build (requires rapids-cmake).
          # Install headers + a minimal CMake config so find_package(stdexec) works.
          stdexecPkg = stdenv.mkDerivation {
            pname         = "stdexec";
            version       = "unstable";
            src           = stdexec;
            dontBuild     = true;
            dontConfigure = true;
            installPhase  = ''
              mkdir -p $out/include
              cp -r include/stdexec $out/include/
              cp -r include/exec    $out/include/
              mkdir -p $out/lib/cmake/stdexec
              cat > $out/lib/cmake/stdexec/stdexecConfig.cmake << EOF
              if(NOT TARGET STDEXEC::stdexec)
                add_library(STDEXEC::stdexec INTERFACE IMPORTED)
                target_include_directories(STDEXEC::stdexec INTERFACE "$out/include")
              endif()
              EOF
            '';
          };

          iris = stdenv.mkDerivation {
            pname = "iris"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ jdk pkgs.gtest stdexecPkg ];
            # openjdk's setup hook sets JAVA_HOME to ${jdk}/lib/openjdk, but
            # CMakeLists.txt expects ${jdk} as the root (it appends /lib/openjdk itself).
            preConfigure      = "export JAVA_HOME=${jdk}";
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release" "-DIRIS_STDEXEC=ON" ];
            installPhase      = "cmake --install . --prefix $out";
            meta.description  = "Iris core library — runtime type bus with JVM backend.";
            meta.platforms    = pkgs.lib.platforms.linux;
          };

          iris-tests = stdenv.mkDerivation {
            pname = "iris-tests"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ jdk pkgs.gtest iris stdexecPkg ];
            preConfigure      = "export JAVA_HOME=${jdk}";
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Debug" "-DIRIS_STDEXEC=ON" ];
            doCheck           = true;
            checkPhase        = "export JAVA_HOME=${jdk}; ctest --output-on-failure -j$(nproc)";
            installPhase      = "mkdir -p $out/bin; cp iris_tests $out/bin/";
          };
        in {
          packages  = { default = iris; inherit iris iris-tests; };
          devShells.default = pkgs.mkShell.override { inherit stdenv; } {
            name     = "iris-dev";
            packages = [
              pkgs.cmake pkgs.ninja pkgs.gtest pkgs.pkg-config
              jdk stdexecPkg pkgs.clang-tools pkgs.gdb pkgs.valgrind
            ];
            shellHook = ''
              export JAVA_HOME=${jdk}
              export CC=${stdenv.cc}/bin/gcc
              export CXX=${stdenv.cc}/bin/g++
              echo "iris dev — GCC $(g++ --version | head -1 | grep -oP '\d+\.\d+\.\d+'), JDK $JAVA_HOME"
            '';
          };
        };
    in
    {
      packages  = forAll (system: pkgs: (perSystem system pkgs).packages);
      devShells = forAll (system: pkgs: (perSystem system pkgs).devShells);
    };
}
