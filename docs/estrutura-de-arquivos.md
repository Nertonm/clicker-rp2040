The `firmware` directory contains the main application code, including the entry point and the logic for handling button presses, updating the display, and managing the overall behavior of the device. The `CMakeLists.txt` file in this directory is responsible for building the firmware, linking necessary libraries, and defining compile-time options.

Middleware is for handling the buttons, display, and other components. It abstracts the hardware details and provides a clean interface for the main application logic.

The `button_handler.c` file contains the implementation of the button handling logic, including debouncing and event detection. It interacts with the GPIO pins to read the state of the buttons and provides a way for the main application to respond to button presses.

hardware_config.md is for defining the hardware configuration, such as pin assignments and other hardware-specific settings. This allows for easier maintenance and updates to the hardware configuration without affecting the main application logic.

