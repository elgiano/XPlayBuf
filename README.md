# XPlayBuf

Author: Gianluca Elia

A buffer player that can loop and fade, while not suffering from the "float precision issue"

### Motivations
There is a precision problem when using Phasor/BufRd, which becomes particularly annoying for long buffers or slow play rates.
PlayBuf overcomes it by storing an internal phase as a double, with the drawback that current phase information is not accessible. Since I primarily use playback position to do anti-click fades, I wrote a UGen that does it internally, so that it can both benefit from double precision and not click.

I strongly support [esluyter/super-bufrd](https://github.com/esluyter/super-bufrd) as an effort to manage double precision AND communication of (precise) current phase. It's great and super promising, but it adds a good deal of complexity (e.g. number of UGens, CPU usage...) that I prefer to avoid unless I have a very good reason to want that precise phase information (and not only for anti-click fades). So that I can happily instantiate around a hundred XPlayBufs without saturating my CPU or having too big SynthDefs.

### Features

- All parameters are required in seconds, as opposed to samples;
- Cross-fades when skipping to new positions on trigger;
- Fades to silence when approaching loop start and end positions;
- Loop boundaries are updated only on trigger;
- Modulateable fadeTime, with a choice between linear or equal-power fades.

### TODO
- Loops across buffer boundaries
- Linear and no interpolation
- UnitTests

### Requirements

- CMake >= 3.5
- SuperCollider source code

### Building

Clone the project:

    git clone https://elgiano/xplaybuf
    cd xplaybuf
    mkdir build
    cd build

Then, use CMake to configure and build it:

    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release
    cmake --build . --config Release --target install

You may want to manually specify the install location in the first step to point it at your
SuperCollider extensions directory: add the option `-DCMAKE_INSTALL_PREFIX=/path/to/extensions`.

It's expected that the SuperCollider repo is cloned at `../supercollider` relative to this repo. If
it's not: add the option `-DSC_PATH=/path/to/sc/source`.

### Developing

Use the command in `regenerate` to update CMakeLists.txt when you add or remove files from the
project. You don't need to run it if you only change the contents of existing files. You may need to
edit the command if you add, remove, or rename plugins, to match the new plugin paths. Run the
script with `--help` to see all available options.
