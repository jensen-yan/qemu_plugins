let
    # nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/archive/refs/tags/23.05.tar.gz";
    nixpkgs = fetchTarball {
        url = "https://github.com/NixOS/nixpkgs/archive/d881cf9fd64218a99a64a8bdae1272c3f94daea7.tar.gz";
        sha256 = "sha256:1jaghsmsc05lvfzaq4qcy281rhq3jlx75q5x2600984kx1amwaal";
    };  # 22.05, not use qemu v9.0
    pkgs = import nixpkgs { config = {}; overlays = []; };
in
pkgs.mkShell {
    buildInputs = with pkgs; [
        pkg-config
        glib
        gcc
        which
        qemu    # v7.0
    ];
}