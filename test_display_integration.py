#!/usr/bin/env python3
"""
Test script to demonstrate ESP32 OLED Display integration with MQTT broker
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

def test_display_integration():
    """Test the display module integration"""
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(BROKER, 1883, 60)
        client.loop_start()
        
        print("🧪 Testing ESP32 OLED Display Integration")
        print("=" * 50)
        
        # Test 1: Add X marks
        print("\n1. Testing X mark addition...")
        for i in range(3):
            client.publish("rpi/to/display", json.dumps({"type": "X"}))
            time.sleep(1)
        
        # Test 2: Reset X counter
        print("\n2. Testing X counter reset...")
        client.publish("rpi/to/display", json.dumps({"type": "RESET_X"}))
        time.sleep(1)
        
        # Test 3: Start timer
        print("\n3. Testing timer start...")
        client.publish("rpi/to/display", json.dumps({
            "type": "START_TIMER", 
            "duration": 30
        }))
        time.sleep(2)
        
        # Test 4: Test display
        print("\n4. Testing display...")
        client.publish("rpi/to/display", json.dumps({"type": "TEST"}))
        time.sleep(3)
        
        # Test 5: Stop timer
        print("\n5. Testing timer stop...")
        client.publish("rpi/to/display", json.dumps({"type": "STOP_TIMER"}))
        time.sleep(1)
        
        # Test 6: Victory message
        print("\n6. Testing victory message...")
        client.publish("rpi/to/display", json.dumps({
            "type": "VICTORY",
            "message": "All puzzles completed!",
            "timestamp": str(int(time.time()))
        }))
        time.sleep(2)
        
        # Test 7: Game over message
        print("\n7. Testing game over message...")
        client.publish("rpi/to/display", json.dumps({
            "type": "GAME_OVER",
            "message": "Time's up! Game over!",
            "reason": "timer_finished"
        }))
        time.sleep(2)
        
        print("\n✅ All tests completed!")
        print("\nExpected behavior:")
        print("- X marks should appear on the 128x32 display")
        print("- Timer should countdown on the 128x64 display")
        print("- Test should show both displays")
        print("- Victory/Game Over should show appropriate messages")
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    test_display_integration()
