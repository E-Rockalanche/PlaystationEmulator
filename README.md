# Playstation Emulator

This is an emulator of the Sony Playstation (PSX) written in c++ using SDL2 and OpenGL. This is a personal project started with the goal of playing Final Fantasy VIII for the first time on my own emulator (I am currently playing through the game!). The emulator supports basic controller support, save states, and some graphical enhancements. This emulator is incomplete. Not all games or regions are supported.

A BIOS ROM image is required to run games on the emulator. It must be named "bios.bin" and be placed in the working directory.

Games are loaded by dragging the ROM file into the emulator window. The emulator currently supports ".bin" and ".cue" ROM formats.

A memory card file is automatically loaded or created for each game. Memory cards can also be loaded into slot 1 by dragging them into the emulator window. The emulator currently supports ".mcr", ".mcd", and ".mc" memory card file extensions. If the extension is something else, with the exception of ".gme", then you can probably rename it to any of the supported extensions.

## System Requirements
* An x86_64 CPU
* Windows 10 (other versions of Windows may work but are not supported)
* An OpenGL 3.3 compatible GPU

## Controls
The emulator will detect SDL compatible game controllers (e.g. Xbox 360) and treat the button layout as if it is a playstation analog controller (DualShock not yet supported). Toggle between analog and digital mode by pressing the Home/Guide/etc button on the controller. Games that support or require analog controls will not detect the analog sticks in digital mode.

### Keyboard Bindings
* **Start:** enter
* **Select:** right shift
* **D pad:** up/down/left/right
* **Triangle:** V
* **Circle:** C
* **X:** X
* **Square:** Z
* **L1:** S
* **L2:** A
* **R1:** D
* **R2:** F

### Hotkeys
* **F1:** toggle paused
* **F2:** advance frame while paused
* **F3:** toggle mute
* **F5:** save state
* **F6:** toggle VRAM view
* **F7:** toggle real colour mode
* **F9:** load save state
* **F11:** toggle fullscreen
* **F12:** save screenshot
* **-:** decrement resolution scale
* **+:** increment resolution scale
* **Escape:** reset the console

## Screenshots
![screenshot_1655064858](https://user-images.githubusercontent.com/22203222/173252887-818a8acf-a166-47f7-9b36-d9d88b49df6f.png)
![screenshot_1655065297](https://user-images.githubusercontent.com/22203222/173252902-45cf9270-0e91-4dc4-b76c-67f32db1852a.png)
![screenshot_1655065475](https://user-images.githubusercontent.com/22203222/173252911-f58daba3-9a4c-4e64-b8ee-8e339bd2f649.png)
![screenshot_1655066181](https://user-images.githubusercontent.com/22203222/173252952-00cdde3a-945d-4ac9-8de7-5470a96652db.png)
![screenshot_1655073320](https://user-images.githubusercontent.com/22203222/173256326-49e46b2e-7b7d-4e7c-8990-f99ebbf5fe89.png)
![screenshot_1655073772](https://user-images.githubusercontent.com/22203222/173256542-a8401271-6c1b-487b-9400-444a1537fdef.png)
![screenshot_1655074125](https://user-images.githubusercontent.com/22203222/173256785-9a584589-38d7-417e-8d58-501af3495bc8.png)
![screenshot_1655074603](https://user-images.githubusercontent.com/22203222/173256925-65dcc312-2750-4af1-bc10-52e5fbca6df7.png)

## Features

### VRAM View
The playstation contains 1MB of video RAM organized as 512 lines of 2048 bytes. The VRAM is used to store display buffers, colour tables, and texture data.
![screenshot_1655065655](https://user-images.githubusercontent.com/22203222/173255501-a03e6f9d-5c6f-41b1-8b4f-33564ca63b9f.png)

### Real Colour Mode
The playstation normally renders primitives in 15-bit RGB colour with optional dithering. Real colour mode allows the emulator to render in full 24-bit RGB colour.

Gradients (with and without dithering) with real colour **off**:

![screenshot_1655072277](https://user-images.githubusercontent.com/22203222/173255898-793873ea-5756-4325-9810-13487ab9ff59.png)

Gradients with real colour **on**:

![screenshot_1655072300](https://user-images.githubusercontent.com/22203222/173255902-fbcc05b7-3dcb-41aa-94b3-92c344a03076.png)

### Resolution Scaling
The emulator supports rendering graphics at higher resolution, up to 8x scale.

**1x** resolution with real colour off:

![screenshot_1655065184](https://user-images.githubusercontent.com/22203222/173256038-c37001c6-8efc-4c26-928b-2c2f06db16e3.png)

**4x** resolution with real colour on:

![screenshot_1655065178](https://user-images.githubusercontent.com/22203222/173256041-ad60f3b0-19e1-41ae-9a07-9f4f873f9e61.png)
