{
    description = "Silicate replay format";

    inputs = {
	nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    };

    outputs = { self, nixpkgs }:
    let
	system = "x86_64-linux";
	pkgs = import nixpkgs { inherit system; };
	llvm = pkgs.llvmPackages_21;
    in
    {
	devShells.${system} = {
	    default = (pkgs.mkShell.override { stdenv = llvm.stdenv; }) {
		packages = with pkgs; [
		    llvm.clang-tools
		    llvm.clang
		    llvm.bintools
		    cmake
		];
	    };
	};
    };
}
