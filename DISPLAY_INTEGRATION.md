# ESP32 OLED Display Integration

This document explains how the ESP32 OLED Display module is integrated with the MQTT bomb defusal game system.

## Overview

The ESP32 OLED module provides dual displays:
- **Timer Display (128x64)**: Shows countdown timer with progress bar
- **X Counter Display (128x32)**: Shows X marks for failures/penalties

## Hardware Setup

### Display 1 (Timer - 128x64)
- VCC → 3.3V
- GND → GND  
- SDA → GPIO21
- SCL → GPIO22
- I2C Address: 0x3C

### Display 2 (X Counter - 128x32)
- VCC → 3.3V
- GND → GND
- SDA → GPIO25
- SCL → GPIO26
- I2C Address: 0x3C

## MQTT Integration

### Topics
- **Subscribe**: `rpi/to/display` - Commands from Raspberry Pi
- **Publish**: `display/to/rpi` - Status updates to Raspberry Pi

### Message Types

#### From Raspberry Pi to Display:
```json
{
  "type": "START_TIMER",
  "duration": 300
}
```

```json
{
  "type": "STOP_TIMER"
}
```

```json
{
  "type": "X"
}
```

```json
{
  "type": "RESET_X"
}
```

```json
{
  "type": "TEST"
}
```

```json
{
  "type": "VICTORY",
  "message": "All puzzles completed!"
}
```

```json
{
  "type": "GAME_OVER",
  "message": "Time's up! Game over!"
}
```

#### From Display to Raspberry Pi:
```json
{
  "type": "DISPLAY_CONNECTED",
  "device": "ESP32_Display",
  "x_count": 0,
  "max_x_count": 3
}
```

```json
{
  "type": "X_ADDED",
  "message": "X mark added, total: 1/3"
}
```

```json
{
  "type": "MAX_X_REACHED",
  "message": "Maximum X count reached: 3"
}
```

```json
{
  "type": "TIMER_STARTED",
  "message": "Countdown started: 300 seconds"
}
```

```json
{
  "type": "TIMER_FINISHED",
  "message": "Countdown completed"
}
```

## Usage

### Starting the System

1. **Start the MQTT broker** (Raspberry Pi):
   ```bash
   python3 mqtt.py
   ```

2. **Upload and run the ESP32 code**:
   - Open `esp32-oled-module.ino` in Arduino IDE
   - Update WiFi credentials if needed
   - Upload to ESP32

3. **Test the integration**:
   ```bash
   python3 test_display_integration.py
   ```

### Available Commands

When the MQTT broker is running, you can use these commands in the Python console:

```python
# Game control
start_all_games()      # Start all games including display timer
stop_all_games()       # Stop all games
reset_all_games()      # Reset all games

# Display control
add_x()                # Add X mark to display
reset_x()              # Reset X counter
start_timer(300)       # Start 300-second timer
stop_timer()           # Stop timer
test_display()         # Test both displays

# Settings
update_timer(game_duration=300, warning_time=60)
```

## Game Flow Integration

### Starting a Game
1. `start_all_games()` sends `START_TIMER` to display
2. Display shows countdown on 128x64 screen
3. X counter resets to 0 on 128x32 screen

### During Gameplay
- Wrong wire cuts → Send `X` to display
- Wall hits in maze → Send `X` to display  
- Button game failures → Send `X` to display
- Display shows X marks on 128x32 screen

### Game Completion
- All games completed → Send `VICTORY` to display
- Timer expires → Send `GAME_OVER` to display
- Display shows appropriate end message

### Maximum X Reached
- When X count reaches maximum (default 3):
  - Display shows "MAX LIMIT!" warning
  - Sends `MAX_X_REACHED` to Raspberry Pi
  - Can trigger game over logic

## Configuration

The display module respects the timer settings from the config file:

```json
{
  "timer_settings": {
    "game_duration": 300,
    "warning_time": 60,
    "countdown_start": 10,
    "auto_reset": true,
    "difficulty_level": "medium"
  }
}
```

## Troubleshooting

### Display Not Working
1. Check I2C wiring (SDA/SCL pins)
2. Verify I2C addresses (both displays use 0x3C)
3. Check power supply (3.3V)
4. Monitor serial output for error messages

### MQTT Connection Issues
1. Verify WiFi credentials in ESP32 code
2. Check Raspberry Pi IP address
3. Ensure MQTT broker is running
4. Check network connectivity

### Display Shows Wrong Content
1. Verify topic names match between ESP32 and Python
2. Check JSON message format
3. Monitor MQTT messages with MQTT Explorer

## Serial Commands

The ESP32 also accepts serial commands for testing:

- `x` - Add X mark
- `reset` - Reset X counter  
- `start 30` - Start 30-second timer
- `stop` - Stop timer
- `test` - Test both displays
- `mqtt` - Show MQTT status
- `max 5` - Set max X count to 5
