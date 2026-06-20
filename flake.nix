{
  description = "Iris — universal runtime type bus. PoC: core IR + JVM backend.";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.stdexec = { url = "github:NVIDIA/stdexec"; flake = false; };
  inputs.replxx  = { url = "github:AmokHuginnsson/replxx"; flake = false; };

  outputs = { self, nixpkgs, stdexec, replxx }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll  = f: nixpkgs.lib.genAttrs systems (system: f system (import nixpkgs { inherit system; }));
      version = "0.1.0";

      perSystem = system: pkgs:
        let
          stdenv = pkgs.gcc16Stdenv;
          jdk    = pkgs.openjdk21;

          replxxStatic = stdenv.mkDerivation {
            pname         = "replxx-static";
            version       = "0.0.4";
            src           = replxx;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            cmakeFlags    = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release"
                              "-DBUILD_SHARED_LIBS=OFF"
                              "-DREPLXX_BUILD_EXAMPLES=OFF"
                              "-DREPLXX_BUILD_PACKAGE=OFF" ];
            installPhase  = "cmake --install . --prefix $out";
          };

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
            buildInputs       = [ jdk stdexecPkg ];
            preConfigure      = "export JAVA_HOME=${jdk}";
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release"
                                  "-DBUILD_SHARED_LIBS=ON"
                                  "-DIRIS_STDEXEC=ON"
                                  "-DIRIS_BUILD_TESTS=OFF" ];
            installPhase      = "cmake --install . --prefix $out";
            meta.description  = "Iris core library — runtime type bus with JVM backend.";
            meta.platforms    = pkgs.lib.platforms.linux;
          };

          iris-tests = stdenv.mkDerivation {
            pname = "iris-tests"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ pkgs.gtest ];
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Debug"
                                  "-DBUILD_SHARED_LIBS=ON"
                                  "-DIRIS_JAVA_BACKEND=OFF"
                                  "-DIRIS_STDEXEC=OFF"
                                  "-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
                                  "-DCMAKE_INSTALL_RPATH=${placeholder "out"}/lib:${pkgs.gtest}/lib" ];
            installPhase      = ''
              mkdir -p $out/bin $out/lib
              cp iris_core_tests $out/bin/iris_tests
              cp libiris.so      $out/lib/
              cp libirisos.so    $out/lib/
            '';
          };

          irish = stdenv.mkDerivation {
            pname = "irish"; inherit version; src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs       = [ replxxStatic stdexecPkg ];
            cmakeFlags        = [ "-GNinja" "-DCMAKE_BUILD_TYPE=Release"
                                  "-DBUILD_SHARED_LIBS=OFF"
                                  "-DIRIS_IRISH=ON" "-DIRIS_OS_BACKEND=ON"
                                  "-DIRIS_JAVA_BACKEND=OFF" "-DIRIS_BUILD_TESTS=OFF"
                                  "-DIRIS_STATIC_RUNTIME=ON" ];
            installPhase      = "mkdir -p $out/bin; cp irish $out/bin/";
            postFixup         = ''
              patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
                       --set-rpath ""  $out/bin/irish
            '';
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
