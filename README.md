# T-Display-S3 Analog Readings

An analog graph display for the LilyGO T-Display-S3 featuring:
- Mode switching between simulated & real-time sensor graphing
- NTP time synchronization
- Power voltage monitoring (USB and battery)
- Performance monitoring (FPS counter)
- Statistics info with resetting

## Features

- Dual operating modes selection via the KEY button:
  - Simulation mode (0) with random value generation
  - Real time sensor mode (1), reading from GPIO1 (ADC1 channel 0)
- NTP time synchronization with automatic DST handling
- Battery & USB voltage monitoring (whichever is connected)
- Performance monitoring (FPS counter)
- Min/Max/Current value tracking with timestamps
  - Values can be reset via the BOOT button

## Pin Configuration
| Function          | GPIO Pin |
|-------------------|----------|
| Left Button       | 0        |
| Right Button      | 14       |
| Battery Voltage   | 4        |
| LCD Power         | 15       |
| Analog Sensor (S) | 1        |

## Notes

- First build the project in PlatformIO to download the TFT_eSPI library.
- In the ~.pio\libdeps\lilygo-t-display-s3\TFT_eSPI\User_Setup_Select.h file, make sure to:
  - comment out line 27 (#include <User_Setup.h>) and,
  - uncomment line 133 (#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>)
- Only once the User_Setup_Select.h has been modified should the code be uploaded to the T-Display-S3.

## Credits

This project is inspired by [Volos Projects - AnalogReadings](https://github.com/VolosR/NewTTGOAnalogReadings)
