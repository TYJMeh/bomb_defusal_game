#!/usr/bin/env python3
"""
Test script to demonstrate ESP32 Wire Module integration with MQTT broker
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

def test_wire_integration():
    """Test the wire module integration"""
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(BROKER, 1883, 60)
        client.loop_start()
        
        print("🧪 Testing ESP32 Wire Module Integration")
        print("=" * 50)
        
        # Test 1: Start wire game
        print("\n1. Testing wire game start...")
        client.publish("rpi/to/esp", json.dumps({
            "command": "START_GAME",
            "reset": True
        }))
        time.sleep(2)
        
        # Test 2: Simulate wrong wire cut
        print("\n2. Testing wrong wire cut simulation...")
        client.publish("rpi/to/esp", json.dumps({
            "type": "WRONG_CUT_ALERT",
            "wrong_wire_cut": "BLUE",
            "expected_wire": "RED",
            "current_step": 1,
            "total_steps": 4
        }))
        time.sleep(2)
        
        # Test 3: Send X penalty
        print("\n3. Testing X penalty...")
        client.publish("rpi/to/esp", json.dumps({"command": "X"}))
        time.sleep(2)
        
        # Test 4: Reset game
        print("\n4. Testing game reset...")
        client.publish("rpi/to/esp", json.dumps({"command": "RESET_GAME"}))
        time.sleep(2)
        
        # Test 5: Stop game
        print("\n5. Testing game stop...")
        client.publish("rpi/to/esp", json.dumps({"command": "STOP_GAME"}))
        time.sleep(2)
        
        # Test 6: Simple commands
        print("\n6. Testing simple commands...")
        client.publish("rpi/to/esp", "START")
        time.sleep(1)
        client.publish("rpi/to/esp", "STOP")
        time.sleep(1)
        client.publish("rpi/to/esp", "RESET")
        time.sleep(2)
        
        print("\n✅ All wire module tests completed!")
        print("\nExpected behavior:")
        print("- Wire game should start when receiving START_GAME command")
        print("- Wrong cuts should send WRONG_CUT_ALERT to display module")
        print("- X penalty should deactivate the wire game")
        print("- Game should reset and stop on commands")
        print("- All events should be logged in serial output")
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    test_wire_integration()
