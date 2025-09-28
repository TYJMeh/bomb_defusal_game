#!/usr/bin/env python3
"""
Complete integration test for all ESP32 modules with OLED display
Run this after starting the main mqtt.py broker
"""

import paho.mqtt.client as mqtt
import json
import time

BROKER = "192.168.1.201"

def on_connect(client, userdata, flags, rc):
    print(f"Test client connected with result code {rc}")

def on_message(client, userdata, msg):
    print(f"Received: {msg.topic} -> {msg.payload.decode()}")

def test_complete_integration():
    """Test the complete integration of all modules"""
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(BROKER, 1883, 60)
        client.loop_start()
        
        print("🧪 Testing Complete ESP32 Module Integration")
        print("=" * 60)
        
        # Test 1: Start all games
        print("\n1. Testing complete game start...")
        client.publish("rpi/to/esp", json.dumps({
            "command": "START_GAME",
            "reset": True
        }))
        client.publish("rpi/to/esp2", json.dumps({
            "command": "START_TIMER",
            "settings": {"game_duration": 300}
        }))
        client.publish("rpi/to/esp3", json.dumps({
            "command": "START_GAME",
            "reset_position": True
        }))
        client.publish("rpi/to/esp4", json.dumps({
            "command": "START_GAME",
            "new_round": True
        }))
        client.publish("rpi/to/display", json.dumps({
            "type": "START_TIMER",
            "duration": 300
        }))
        time.sleep(3)
        
        # Test 2: Simulate wire game wrong cut
        print("\n2. Testing wire game wrong cut...")
        client.publish("rpi/to/esp", json.dumps({
            "type": "WRONG_CUT_ALERT",
            "wrong_wire_cut": "BLUE",
            "expected_wire": "RED",
            "current_step": 1,
            "total_steps": 4
        }))
        time.sleep(2)
        
        # Test 3: Simulate maze wall hit
        print("\n3. Testing maze wall hit...")
        client.publish("rpi/to/esp3", json.dumps({
            "type": "WALL_HIT",
            "message": "Player hit wall and returned to start",
            "device": "ESP32_Maze"
        }))
        time.sleep(2)
        
        # Test 4: Simulate button game loss
        print("\n4. Testing button game loss...")
        client.publish("rpi/to/esp4", json.dumps({
            "type": "BUTTON_GAME_LOST",
            "message": "Player lost the button timing game!",
            "press_duration": 1500,
            "target_time": 2000,
            "difference": 500
        }))
        time.sleep(2)
        
        # Test 5: Add X marks to display
        print("\n5. Testing X mark addition...")
        for i in range(3):
            client.publish("rpi/to/display", json.dumps({"type": "X"}))
            time.sleep(1)
        
        # Test 6: Reset display
        print("\n6. Testing display reset...")
        client.publish("rpi/to/display", json.dumps({"type": "RESET_X"}))
        time.sleep(1)
        
        # Test 7: Simulate wire game completion
        print("\n7. Testing wire game completion...")
        client.publish("rpi/to/esp", json.dumps({
            "type": "PUZZLE_COMPLETED",
            "message": "Wire cutting puzzle completed successfully!"
        }))
        time.sleep(2)
        
        # Test 8: Simulate maze completion
        print("\n8. Testing maze completion...")
        client.publish("rpi/to/esp3", json.dumps({
            "type": "MAZE_COMPLETED",
            "message": "Player completed the maze!",
            "device": "ESP32_Maze"
        }))
        time.sleep(2)
        
        # Test 9: Simulate button game win
        print("\n9. Testing button game win...")
        client.publish("rpi/to/esp4", json.dumps({
            "type": "BUTTON_GAME_WON",
            "message": "Player won the button timing game!",
            "press_duration": 2000,
            "target_time": 2000,
            "difference": 0
        }))
        time.sleep(2)
        
        # Test 10: Test display functions
        print("\n10. Testing display functions...")
        client.publish("rpi/to/display", json.dumps({"type": "TEST"}))
        time.sleep(3)
        
        # Test 11: Victory message
        print("\n11. Testing victory message...")
        client.publish("rpi/to/display", json.dumps({
            "type": "VICTORY",
            "message": "All puzzles completed!",
            "timestamp": str(int(time.time()))
        }))
        time.sleep(2)
        
        # Test 12: Game over message
        print("\n12. Testing game over message...")
        client.publish("rpi/to/display", json.dumps({
            "type": "GAME_OVER",
            "message": "Time's up! Game over!",
            "reason": "timer_finished"
        }))
        time.sleep(2)
        
        print("\n✅ All integration tests completed!")
        print("\nExpected behavior:")
        print("- All modules should connect and send status messages")
        print("- Wrong cuts and failures should add X marks to display")
        print("- Game completions should be tracked")
        print("- Display should show timer countdown and X marks")
        print("- Victory/Game Over should show appropriate messages")
        print("- All events should be logged in serial output")
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    test_complete_integration()
