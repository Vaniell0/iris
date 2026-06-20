{
  description = "Iris — universal runtime type bus. PoC: core IR + JVM backend.";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.stdexec      = { url = "github:NVIDIA/stdexec"; flake = false; };

  outputs = { self, nixpkgs, stdexec }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll  = f: nixpkgs.lib.genAttrs systems (system: f system (import nixpkgs { inherit system; }));
      version = "0.1.0";

      perSystem = system: pkgs:
        let
          stdenv = pkgs.gcc16Stdenv;
          jdk    = pkgs.openjdk21;

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
            preConfigure      = "export JAVA_HOME=${jdk}";
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release" "-DIRIS_STDEXEC=ON" "-DIRIS_STDMETA=ON" ];
            installPhase      = "cmake --install . --prefix $out";
            meta.description  = "Iris core library — runtime type bus with JVM backend.";
            meta.platforms    = pkgs.lib.platforms.linux;
          };

          iris-tests = stdenv.mkDerivation {
            pname = "iris-tests"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ jdk pkgs.gtest iris stdexecPkg ];
            preConfigure      = "export JAVA_HOME=${jdk}";
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Debug" "-DIRIS_STDEXEC=ON" "-DIRIS_STDMETA=ON" ];
            doCheck           = true;
            checkPhase        = "export JAVA_HOME=${jdk}; ctest --output-on-failure -j$(nproc)";
            installPhase      = "mkdir -p $out/bin; cp iris_core_tests $out/bin/iris_tests";
          };

          irish = stdenv.mkDerivation {
            pname = "irish"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ pkgs.replxx stdexecPkg ];
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release"
                                  "-DIRIS_IRISH=ON" "-DIRIS_OS_BACKEND=ON"
                                  "-DIRIS_JAVA_BACKEND=OFF" "-DIRIS_BUILD_TESTS=OFF" ];
            installPhase      = "mkdir -p $out/bin; cp irish $out/bin/";
            meta.description  = "irish — irsh language interpreter and REPL";
            meta.mainProgram  = "irish";
            meta.platforms    = pkgs.lib.platforms.linux;
          };

        in {
          packages  = { default = irish; inherit iris iris-tests irish; };
          devShells.default = pkgs.mkShell.override { inherit stdenv; } {
            name     = "iris-dev";
            packages = [
              pkgs.cmake pkgs.ninja pkgs.gtest pkgs.pkg-config
              jdk stdexecPkg pkgs.clang-tools pkgs.gdb pkgs.valgrind
              pkgs.replxx
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
