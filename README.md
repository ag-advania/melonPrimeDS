<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonDS: Metroid Hunters</b></h1>
<br>
    
Modded version of melonDS emulator to play Metroid Hunters.

It's a bit of a hack but the goal is to make the game as fun as possible using mouse and keyboard.

### Main instructions

-   Make sure to set all bindings to `None` in<br>
    `Config → Input and hotkeys → DS keypad`<br>
    _(click binding and press backspace)_

-   **Will be updated soon! Hunters needs precise input like a mouse because there's no lock-on, so using a controller is a bad idea**

    <strike>Set Metroid Hunters related `Joystick mappings` in<br>
    `Config → Input and hotkeys → Add-ons`<br>
    Recommended Nintendo layout controls have been added in parentheses.

    Notes:

    -   Left and Right trigger are the same button.
    -   UI OK (A) will press "OK" on the touch screen, which will also jump and briefly break aiming.<br>
        Just be mindful that the dedicated jump button (B) is what you should use.
    -   UI left and right will also press on the touch screen, for scan visor messages
    -   Cycle weapon (X) will try to select the 3rd weapon which you won't have yet when you start
    -   When in map view, press (Y) to zoom out and (LT/RT) to zoom in

    <br>
    <img src="./metroid/hunters%20controls.png" height="100"/>

</strike>

### Optional instructions

-   Enable JIT to improve performance<br>
    `Config → Emu settings → CPU emulation → Enable JIT recompiler`

-   Render game at a high resolution<br>

    -   Disable `Config → Screen filtering`<br>
    -   `Config → Video settings`<br>
        Set `3D renderer` to `OpenGL`<br>
        Disable `VSync` for lower latency<br>
        Set `Internal resolution` to next highest for your monitor

-   My recommended screen layout<br>
    `Config → Screen layout → Horizontal`<br>
    `Config → Screen sizing → Emphasize top`<br>

-   Bind `Toggle fullscreen` to `F11` or something else<br>
    `Config → Input and hotkeys → General hotkeys`
