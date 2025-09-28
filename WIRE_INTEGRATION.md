# ESP32 Wire Module MQTT Integration

This document explains how the ESP32 Wire Module is integrated with the MQTT bomb defusal game system.

## Overview

The ESP32 Wire Module provides a wire cutting puzzle game with:
- **4 Wires**: RED, BLUE, GREEN, YELLOW (configurable)
- **Sequential Puzzle**: Must cut wires in correct order
- **Penalty System**: Wrong cuts trigger penalties
- **MQTT Integration**: Full communication with game coordinator

## Hardware Setup

### Wire Connections
- **Wire 1 (RED)**: GPIO 5 → GND (when cut)
- **Wire 2 (BLUE)**: GPIO 18 → GND (when cut)  
- **Wire 3 (GREEN)**: GPIO 19 → GND (when cut)
- **Wire 4 (YELLOW)**: GPIO 23 → GND (when cut)
- **Status LED**: GPIO 2 (built-in LED)

### Wiring Notes
- Wires are connected between GPIO pins and GND
- When intact: HIGH (pulled up by internal resistor)
- When cut: LOW (floating, pulled down by internal logic)
- Use INPUT_PULLUP mode for proper operation

## MQTT Integration

### Topics
- **Subscribe**: `rpi/to/esp` - Commands from Raspberry Pi
- **Publish**: `esp/to/rpi` - Status updates to Raspberry Pi

### Message Types

#### From Raspberry Pi to Wire Module:
```json
{
  "command": "START_GAME",
  "reset": true
}
```

```json
{
  "command": "STOP_GAME"
}
```

```json
{
  "command": "RESET_GAME"
}
```

```json
{
  "command": "X"
}
```

#### From Wire Module to Raspberry Pi:
```json
{
  "type": "WIRE_MODULE_CONNECTED",
  "message": "Wire cutting module ready",
  "device": "ESP32_Wire",
  "game_active": false,
  "game_completed": false,
  "current_step": 1,
  "total_steps": 4,
  "wrong_cuts": 0
}
```

```json
{
  "type": "GAME_STARTED",
  "message": "Wire cutting puzzle started"
}
```

```json
{
  "type": "WRONG_CUT_ALERT",
  "wrong_wire_cut": "BLUE",
  "expected_wire": "RED",
  "current_step": 1,
  "total_steps": 4,
  "timestamp": 1234567890
}
```

```json
{
  "type": "PUZZLE_COMPLETED",
  "message": "Wire cutting puzzle completed successfully!"
}
```

```json
{
  "type": "GAME_STATUS",
  "game_active": true,
  "game_completed": false,
  "current_step": 2,
  "total_steps": 4,
  "games_completed": 0,
  "wrong_cuts": 1,
  "current_instruction": "STEP 2: Now cut the BLUE wire",
  "required_wire_color": "BLUE"
}
```

## Game Flow Integration

### Starting a Game
1. `start_all_games()` sends `START_GAME` to wire module
2. Wire module resets and starts puzzle sequence
3. Sends `GAME_STARTED` confirmation to Raspberry Pi

### During Gameplay
- Player cuts correct wire → Game advances to next step
- Player cuts wrong wire → Sends `WRONG_CUT_ALERT` to Raspberry Pi
- Raspberry Pi sends `X` to display module for penalty
- Game continues (doesn't stop on wrong cuts)

### Game Completion
- All steps completed → Sends `PUZZLE_COMPLETED` to Raspberry Pi
- Raspberry Pi stops timer and checks other games
- Wire game becomes inactive

### Penalty System
- Wrong cuts trigger `WRONG_CUT_ALERT` messages
- Raspberry Pi adds X marks to display
- Wire game continues (unlike other modules)
- Can be deactivated with `X` command if needed

## Configuration

### Customizing the Puzzle
Edit the `puzzle_sequence[]` array in the code:

```cpp
PuzzleStep puzzle_sequence[] = {
  {
    "STEP 1: Cut the RED wire to begin",
    0,  // RED wire (index 0)
    "RED wire cut correctly! Next step..."
  },
  {
    "STEP 2: Now cut the BLUE wire",
    1,  // BLUE wire (index 1) 
    "BLUE wire cut correctly! Next step..."
  },
  // Add more steps...
};
```

### Wire Pin Configuration
Change the `WIRE_PINS` array to use different GPIO pins:

```cpp
const int WIRE_PINS[NUM_WIRES] = {5, 18, 19, 23};
```

### Wire Colors
Modify the `wire_colors` array:

```cpp
String wire_colors[NUM_WIRES] = {
  "RED", "BLUE", "GREEN", "YELLOW"
};
```

## Usage

### Starting the System

1. **Start the MQTT broker** (Raspberry Pi):
   ```bash
   python3 mqtt.py
   ```

2. **Upload and run the ESP32 code**:
   - Open `esp32-wire-module.ino` in Arduino IDE
   - Update WiFi credentials if needed
   - Upload to ESP32

3. **Test the integration**:
   ```bash
   python3 test_wire_integration.py
   ```

### Serial Commands

The wire module accepts these serial commands for testing:

- `START` - Begin the puzzle sequence
- `STATUS` - Show current wire status and game progress
- `JSON` - Output wire status as JSON
- `STATS` - Show game statistics
- `STEPS` - Show the complete puzzle sequence
- `RESET` - Reset statistics and game state
- `HELP` - Show help message

### MQTT Commands

When the MQTT broker is running, you can control the wire module:

```python
# Start wire game
client.publish("rpi/to/esp", json.dumps({
    "command": "START_GAME",
    "reset": True
}))

# Stop wire game
client.publish("rpi/to/esp", json.dumps({
    "command": "STOP_GAME"
}))

# Reset wire game
client.publish("rpi/to/esp", json.dumps({
    "command": "RESET_GAME"
}))

# Deactivate wire game (penalty)
client.publish("rpi/to/esp", json.dumps({
    "command": "X"
}))
```

## Status LED Indicators

The built-in LED shows game status:
- **Slow blink**: Idle (no active game)
- **Double heartbeat**: Active game in progress
- **Fast flashing**: Game completed (celebration)

## Troubleshooting

### Wire Detection Issues
1. Check wiring (GPIO to GND)
2. Verify INPUT_PULLUP mode
3. Test with multimeter (should be HIGH when intact, LOW when cut)
4. Check for loose connections

### MQTT Connection Issues
1. Verify WiFi credentials
2. Check Raspberry Pi IP address
3. Ensure MQTT broker is running
4. Monitor serial output for connection status

### Game Logic Issues
1. Check puzzle sequence configuration
2. Verify wire pin assignments
3. Test with serial commands first
4. Monitor MQTT messages

## Statistics Tracking

The module tracks:
- Total wire cuts
- Total reconnections
- Games completed
- Wrong cuts
- Cut accuracy percentage
- Current game progress

Access via `STATS` serial command or MQTT status messages.

## Integration with Other Modules

The wire module integrates with:
- **Display Module**: Receives X penalties for wrong cuts
- **Timer Module**: Stops when puzzle completed
- **Maze Module**: Can trigger penalties
- **Button Module**: Can trigger penalties

All modules work together through the MQTT broker for a complete bomb defusal experience.
