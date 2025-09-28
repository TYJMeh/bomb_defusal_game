# Complete ESP32 Bomb Defusal Game Integration

This document explains the complete integration of all ESP32 modules with the MQTT bomb defusal game system.

## System Overview

The complete bomb defusal game consists of 4 ESP32 modules and 1 Raspberry Pi coordinator:

### ESP32 Modules
1. **Wire Module** (`esp32-wire-module`) - Sequential wire cutting puzzle
2. **Display Module** (`esp32-oled-module`) - Dual OLED displays (timer + X counter)
3. **Maze Module** (`esp32-maze-module`) - Joystick-controlled maze navigation
4. **Button Module** (`esp32-button-module`) - Timing-based button press game

### Raspberry Pi Coordinator
- **MQTT Broker** (`mqtt.py`) - Central communication hub
- **Game Logic** - Coordinates all modules and tracks completion

## MQTT Topic Structure

### Topics
- `rpi/to/esp` - Commands to wire module
- `rpi/to/esp2` - Commands to timer module (display)
- `rpi/to/esp3` - Commands to maze module
- `rpi/to/esp4` - Commands to button module
- `rpi/to/display` - Commands to display module
- `esp/to/rpi` - Messages from wire module
- `esp2/to/rpi` - Messages from timer module
- `esp3/to/rpi` - Messages from maze module
- `esp4/to/rpi` - Messages from button module
- `display/to/rpi` - Messages from display module

## Game Flow Integration

### 1. Game Start
```python
# Start all modules simultaneously
start_all_games()
```
- All modules receive start commands
- Display starts countdown timer
- All games become active
- X counter resets to 0

### 2. During Gameplay
- **Wire Module**: Players cut wires in sequence
- **Maze Module**: Players navigate to end point
- **Button Module**: Players time button press correctly
- **Display Module**: Shows timer countdown and X marks

### 3. Penalty System
When players make mistakes:
- **Wrong wire cut** → X mark added to display
- **Maze wall hit** → X mark added to display
- **Button timing failure** → X mark added to display
- **Maximum X reached** → Game over

### 4. Game Completion
- **All modules complete** → Victory message on display
- **Timer expires** → Game over message on display
- **Maximum X reached** → Game over message on display

## Module Integration Details

### Wire Module Integration
```cpp
// Sends these messages to Raspberry Pi:
- WIRE_MODULE_CONNECTED
- GAME_STARTED
- WRONG_CUT_ALERT (triggers X penalty)
- PUZZLE_COMPLETED (stops timer)
- GAME_STATUS (continuous updates)
```

### Display Module Integration
```cpp
// Receives these commands from Raspberry Pi:
- START_TIMER (starts countdown)
- STOP_TIMER (stops countdown)
- X (adds X mark)
- RESET_X (resets X counter)
- VICTORY (shows victory message)
- GAME_OVER (shows game over message)
```

### Maze Module Integration
```cpp
// Sends these messages to Raspberry Pi:
- MAZE_MODULE_CONNECTED
- WALL_HIT (triggers X penalty)
- MAZE_COMPLETED (stops timer)
- GAME_RESTART (restarts game)
```

### Button Module Integration
```cpp
// Sends these messages to Raspberry Pi:
- BUTTON_MODULE_CONNECTED
- BUTTON_GAME_WON (stops timer)
- BUTTON_GAME_LOST (triggers X penalty)
```

## Message Flow Examples

### Wrong Wire Cut Flow
1. Player cuts wrong wire
2. Wire module → `WRONG_CUT_ALERT` → Raspberry Pi
3. Raspberry Pi → `X` → Display module
4. Display shows X mark and updates counter

### Game Completion Flow
1. All modules complete their puzzles
2. Each module → `*_COMPLETED` → Raspberry Pi
3. Raspberry Pi → `VICTORY` → Display module
4. Display shows victory message

### Timer Expiry Flow
1. Display timer reaches 0
2. Display module → `TIMER_FINISHED` → Raspberry Pi
3. Raspberry Pi → `GAME_OVER` → All modules
4. All modules stop and display shows game over

## Hardware Setup

### Wire Module
- **Wires**: GPIO 5, 18, 19, 23 → GND
- **LED**: GPIO 2 (status indicator)

### Display Module
- **Timer Display**: SDA=21, SCL=22 (128x64)
- **X Counter Display**: SDA=25, SCL=26 (128x32, rotated 180°)

### Maze Module
- **Joystick**: X=GPIO 36, Y=GPIO 33, Button=GPIO 25
- **Display**: SDA=21, SCL=22 (128x64)

### Button Module
- **Button**: GPIO 23 (INPUT_PULLUP)
- **LEDs**: GPIO 32 (WS2812 strip, 20 LEDs)

## Usage

### Starting the Complete System

1. **Start MQTT Broker**:
   ```bash
   python3 mqtt.py
   ```

2. **Upload ESP32 Code**:
   - Upload each module's code to respective ESP32
   - Update WiFi credentials if needed

3. **Test Integration**:
   ```bash
   python3 test_complete_integration.py
   ```

### Available Commands

When MQTT broker is running:

```python
# Game control
start_all_games()      # Start all games
stop_all_games()       # Stop all games
reset_all_games()      # Reset all games

# Display control
add_x()                # Add X mark
reset_x()              # Reset X counter
start_timer(300)       # Start 300-second timer
stop_timer()           # Stop timer
test_display()         # Test displays

# Individual module control
send_maze_command(client, "START_GAME")
send_button_game_command(client, "START_GAME")
```

## Configuration

### Timer Settings
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

### Game Settings
```json
{
  "game_settings": {
    "simultaneous_start": true,
    "require_all_connected": true,
    "emergency_stop_on_disconnect": true
  }
}
```

## Troubleshooting

### Common Issues

1. **Module Not Connecting**
   - Check WiFi credentials
   - Verify MQTT broker is running
   - Check network connectivity

2. **Display Not Working**
   - Check I2C wiring
   - Verify display addresses
   - Check power supply

3. **Games Not Starting**
   - Check MQTT topic names
   - Verify message format
   - Monitor serial output

4. **X Marks Not Appearing**
   - Check MQTT message flow
   - Verify display module is connected
   - Check message format

### Debug Commands

```python
# Check MQTT status
printMQTTStatus()

# Test individual modules
test_display()
send_maze_command(client, "TEST")
send_button_game_command(client, "TEST")

# Monitor messages
# Use MQTT Explorer or similar tool
```

## Performance Considerations

- **MQTT QoS**: Use QoS 0 for better performance
- **Message Frequency**: Limit status updates to prevent flooding
- **Network Stability**: Ensure stable WiFi connection
- **Power Management**: Consider power consumption for battery operation

## Future Enhancements

- **Sound Effects**: Add buzzer for audio feedback
- **Difficulty Levels**: Adjustable puzzle difficulty
- **Score Tracking**: Persistent high scores
- **Multiplayer**: Multiple bomb defusal teams
- **Web Interface**: Remote monitoring and control

## Safety Notes

- **Electrical Safety**: Use proper insulation for wire connections
- **Heat Management**: Ensure adequate ventilation for ESP32 modules
- **Power Supply**: Use appropriate voltage and current ratings
- **Emergency Stop**: Implement emergency stop functionality

This complete integration provides a fully functional bomb defusal game with real-time communication between all modules and visual feedback through the OLED displays.
