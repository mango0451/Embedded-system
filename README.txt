To start MCU Bluetooth:
1. Open the project in STMCubeIDE.
2. Build the project.
3. Plug in STM nucleo into laptop/pc.
4. Run BLE_p2p_server debug.
5. Unplug nucleo.

To start frontend GUI:
1. open project in IDE of choice (I used VS code).
2. open a console, and make sure current directory is "Embedded-system\Frontend" using "cd Frontend."
3. run "npm run dev" in the console (you might need certain vue.js components for it to work, such as node.js, and the vue language support extension on vs code).
4. Open the web address that npm run dev provides in GOOGLE CHROME or EDGE.
5. Use the "Connect" button on the GUI to find, and connect to "P2PSRV1". 

To Use: 
1. Check if GUI time matches real time.
2. Set an alarm time of choice (24 hr format).
3. Press "Set Time & Alarm".
4. Buzzer will buzz 3 times to confirm its armed.
5. When alarm goes off, press button to turn off. 
