import paho.mqtt.client as mqtt
import json
import os
import time

# Track completion status of all games
wire_game_completed = False
maze_game_completed = False
button_game_completed = False

# Track module connections
modules_connected = {
    "wire": False,
    "display": False,
    "maze": False,
    "button": False
}

BROKER = "192.168.1.201"
CONFIG_FILE = "config.json"

def create_default_config():
    """Create a default config file if it doesn't exist"""
    default_config = {
        "timer_settings": {
            "game_duration": 360,
            "warning_time": 60,
            "countdown_start": 10,
            "auto_reset": True,
            "difficulty_level": "medium"
        },
        "game_settings": {
            "simultaneous_start": True,
            "require_all_connected": True,
            "emergency_stop_on_disconnect": True
        }
    }
   
    with open(CONFIG_FILE, "w") as f:
        json.dump(default_config, f, indent=4)
    print(f"Created default config file: {CONFIG_FILE}")
    return default_config

def load_config():
    """Load configuration from file, create default if doesn't exist"""
    try:
        if not os.path.exists(CONFIG_FILE):
            print(f"Config file {CONFIG_FILE} not found. Creating default...")
            return create_default_config()
           
        with open(CONFIG_FILE, "r") as f:
            config = json.load(f)
            print(f"Loaded config from {CONFIG_FILE}")
            return config
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON in {CONFIG_FILE}: {e}")
        print("Creating backup and using default config...")
        # Create backup of corrupted file
        with open(f"{CONFIG_FILE}.backup", "w") as backup:
            with open(CONFIG_FILE, "r") as original:
                backup.write(original.read())
        return create_default_config()
    except Exception as e:
        print(f"Error loading config: {e}")
        return create_default_config()

def save_config(config):
    """Save configuration to file"""
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(config, f, indent=4)
        print(f"Config saved to {CONFIG_FILE}")
        return True
    except Exception as e:
        print(f"Error saving config: {e}")
        return False

def check_all_modules_connected(client):
    """Check if all modules are connected and send activation signal if so"""
    global modules_connected
    
    all_connected = all(modules_connected.values())
    
    if all_connected:
        print("🎉 All modules connected! Sending activation signal...")
        
        # Send activation signal to all modules
        # FIXED: Use proper JSON format with "type" field
        activation_command = {
            "type": "ACTIVATE",
            "message": "All modules connected - system activated!",
            "timestamp": int(time.time() * 1000)  # Add timestamp
        }
        
        # Send to all modules with their specific topics
        topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
        
        for topic in topics:
            client.publish(topic, json.dumps(activation_command))
            print(f"  ✅ Sent activation to {topic}")
        
        print("✅ All activation signals sent!")
        return True
    else:
        missing_modules = [module for module, connected in modules_connected.items() if not connected]
        print(f"⏳ Waiting for modules: {', '.join(missing_modules)}")
        return False

def start_game_timer(client, duration=300):
    """Start the game timer on the display module"""
    timer_command = {
        "type": "START_TIMER",
        "duration": duration,
        "message": f"Game timer started for {duration} seconds"
    }
    
    client.publish("rpi/to/esp2", json.dumps(timer_command))
    print(f"🚀 Game timer started: {duration} seconds")

def send_x_to_display(client, reason="GAME_FAILURE"):
    """Send X mark to display"""
    x_command = {
        "type": "X",
        "message": f"X mark added due to: {reason}"
    }
    
    client.publish("rpi/to/esp2", json.dumps(x_command))
    print(f"❌ X mark sent to display: {reason}")

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe("esp/to/rpi")     # First ESP32 (wire game)
    client.subscribe("esp2/to/rpi")    # Second ESP32 (timer)
    client.subscribe("esp3/to/rpi")    # Third ESP32 (maze game)
    client.subscribe("esp4/to/rpi")    # Fourth ESP32 (button game)
    client.subscribe("display/to/rpi") # ESP32 Display module
    client.subscribe("config/request") # Config requests
    client.subscribe("config/update")  # Config updates

def on_message(client, userdata, msg):
    global button_game_completed, wire_game_completed, maze_game_completed
   
    topic = msg.topic
    message_payload = msg.payload.decode()
    print(f"📨 Received on {topic}: {message_payload}")
   
    # Handle configuration requests
    if topic == "config/request":
        try:
            config = load_config()
            client.publish("config/response", json.dumps(config))
            print("Sent config response")
        except Exception as e:
            print(f"Error handling config request: {e}")
        return
       
    # Handle configuration updates
    elif topic == "config/update":
        try:
            new_config = json.loads(message_payload)
            if save_config(new_config):
                client.publish("config/ack", json.dumps({"status": "success", "message": "Config updated"}))
                print("Config updated successfully")
               
                # Apply new timer settings to timer ESP32
                if "timer_settings" in new_config:
                    timer_update = {
                        "type": "UPDATE_SETTINGS",
                        "settings": new_config["timer_settings"]
                    }
                    client.publish("rpi/to/esp2", json.dumps(timer_update))
                    print("Sent updated timer settings to ESP2")
            else:
                client.publish("config/ack", json.dumps({"status": "error", "message": "Failed to save config"}))
        except json.JSONDecodeError as e:
            print(f"Error parsing config update: {e}")
            client.publish("config/ack", json.dumps({"status": "error", "message": "Invalid JSON"}))
        except Exception as e:
            print(f"Error handling config update: {e}")
            client.publish("config/ack", json.dumps({"status": "error", "message": str(e)}))
        return
   
    try:
        # Try parsing as JSON
        data = json.loads(message_payload)
       
        # Handle messages from ESP32 Display (esp2/to/rpi)
        if topic == "esp2/to/rpi":
            if isinstance(data, dict):
                msg_type = data.get("type", "")
                
                if msg_type == "DISPLAY_CONNECTED":
                    print("📺 Display module connected!")
                    print(f"Device: {data.get('device', 'Unknown')}")
                    modules_connected["display"] = True
                    check_all_modules_connected(client)
                    
                elif msg_type == "DISPLAY_ACTIVATED":
                    print("✅ Display module activated and ready")
                    
                elif msg_type == "TIMER_FINISHED":
                    print("⏰ TIMER FINISHED - GAME OVER!")
                    handle_timer_finished(client)
                    
                elif msg_type == "X_ADDED":
                    print(f"❌ X mark added to display! Total: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
                   
                elif msg_type == "MAX_X_REACHED":
                    print("🚨 DISPLAY: Maximum X count reached!")
                    print(f"X Count: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
                   
                elif msg_type == "TIMER_STARTED":
                    print(f"⏰ Display timer started: {data.get('message', 'Unknown duration')}")
                   
                elif msg_type == "TIMER_STOPPED":
                    print(f"⏹️ Display timer stopped: {data.get('message', 'Unknown reason')}")
        
        # Handle messages from wire module (esp/to/rpi)
        elif topic == "esp/to/rpi":
            if isinstance(data, dict):
                msg_type = data.get("type", "")
                
                if msg_type == "WIRE_MODULE_CONNECTED":
                    print("🔌 Wire module connected!")
                    print(f"Device: {data.get('device', 'Unknown')}")
                    modules_connected["wire"] = True
                    check_all_modules_connected(client)
                    
                elif msg_type == "WRONG_CUT_ALERT":
                    print(f"❌ WRONG CUT ALERT!")
                    print(f"Player cut {data['wrong_wire_cut']} wire")
                    print(f"Expected {data['expected_wire']} wire")
                    print(f"Step: {data['current_step']}/{data['total_steps']}")
                    # Send "X" signal to display ESP32
                    send_x_to_display(client, "WRONG_WIRE_CUT")
                   
                elif msg_type == "PUZZLE_COMPLETED":
                    print("🎉 Wire game completed by first ESP32!")
                    wire_game_completed = True
                    check_all_games_completed(client)
        
        # Handle messages from maze module (esp3/to/rpi)
        elif topic == "esp3/to/rpi":
            if isinstance(data, dict):
                msg_type = data.get("type", "")
                
                if msg_type == "MAZE_MODULE_CONNECTED":
                    print("🎮 Maze module connected!")
                    print(f"Device: {data.get('device', 'Unknown')}")
                    modules_connected["maze"] = True
                    check_all_modules_connected(client)
                    
                elif msg_type == "MAZE_COMPLETED":
                    print("🎉 MAZE COMPLETED!")
                    print(f"Player finished the maze!")
                    maze_game_completed = True
                    check_all_games_completed(client)
                   
                elif msg_type == "WALL_HIT":
                    print(f"❌ WALL HIT IN MAZE!")
                    print(f"Player hit a wall and returned to start")
                    # Send "X" signal to display ESP32
                    send_x_to_display(client, "MAZE_WALL_HIT")
                   
                elif msg_type == "GAME_RESTART":
                    print("🔄 Maze game restarted by player")
        
        # Handle messages from button module (esp4/to/rpi)
        elif topic == "esp4/to/rpi":
            if isinstance(data, dict):
                msg_type = data.get("type", "")
                
                if msg_type == "BUTTON_MODULE_CONNECTED":
                    print("🔘 Button module connected!")
                    print(f"Device: {data.get('device', 'Unknown')}")
                    print(f"Target time: {data.get('target_time', 2000)}ms")
                    modules_connected["button"] = True
                    check_all_modules_connected(client)
                    
                elif msg_type == "BUTTON_GAME_WON":
                    print("🎉 BUTTON GAME WON!")
                    print(f"Player won the button timing game!")
                    print(f"Press duration: {data['press_duration']}ms")
                    print(f"Target time: {data['target_time']}ms")
                    print(f"Difference: {data['difference']}ms")
                    button_game_completed = True
                    check_all_games_completed(client)
                   
                elif msg_type == "BUTTON_GAME_LOST":
                    print("❌ BUTTON GAME LOST!")
                    print(f"Player lost the button timing game!")
                    print(f"Press duration: {data['press_duration']}ms")
                    print(f"Target time: {data['target_time']}ms")
                    print(f"Difference: {data['difference']}ms")
                    # Send "X" signal to display ESP32
                    send_x_to_display(client, "BUTTON_GAME_LOST")
           
    except json.JSONDecodeError as e:
        print(f"❌ JSON decode error: {e}")
        print(f"Raw message: '{message_payload}'")
        
        # Handle non-JSON messages (e.g., "1" from second ESP32)
        message = message_payload.strip()
       
        if message == "1":
            print("Received '1' signal from ESP32")
            # Handle accordingly based on topic
            if topic == "esp2/to/rpi":
                print("Signal from display module")
        else:
            print(f"Unknown non-JSON message: {message}")
    except Exception as e:
        print(f"❌ Error processing message: {e}")

def handle_timer_finished(client):
    """Handle when the display timer finishes - trigger game over"""
    print("💥 TIMER FINISHED - GAME OVER!")
    
    # Send game over signal to all modules
    game_over_message = {
        "type": "GAME_OVER",
        "message": "Time's up! Game over!",
        "reason": "timer_finished"
    }
    
    # Send to all ESP32s including display
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(game_over_message))
    
    print("Game over signals sent to all modules!")

def check_all_games_completed(client):
    """Check if all games are completed and send victory signal"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    if wire_game_completed and maze_game_completed and button_game_completed:
        print("\n🎉 ALL GAMES COMPLETED! VICTORY! 🎉")
       
        # Send victory signal to all ESP32s including display
        victory_message = {
            "type": "VICTORY",
            "message": "All puzzles completed!",
            "timestamp": int(time.time() * 1000)
        }
       
        # Send to all ESP32s including display
        topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
        for topic in topics:
            client.publish(topic, json.dumps(victory_message))
           
        print("Victory signals sent to all ESP32s!")

def start_all_games(client):
    """Start all games simultaneously"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    print("🚀 Starting all games simultaneously...")
   
    # Reset completion status
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
   
    # Load current config for timer settings
    config = load_config()
    timer_settings = config.get("timer_settings", {
        "game_duration": 300,
        "warning_time": 60,
        "countdown_start": 10
    })
   
    # Send start commands to all ESP32s
    start_commands = {
        "rpi/to/esp": {"type": "START_GAME", "reset": True},
        "rpi/to/esp3": {"type": "START_GAME", "reset_position": True},
        "rpi/to/esp4": {"type": "START_GAME", "new_round": True}
    }
   
    # Send countdown signal first
    sync_start = {
        "type": "SYNCHRONIZED_START",
        "countdown": 3
    }
   
    for topic in start_commands.keys():
        client.publish(topic, json.dumps(sync_start))
    
    # Also send to display
    client.publish("rpi/to/esp2", json.dumps(sync_start))
   
    print("Countdown started...")
    time.sleep(3)
   
    # Send actual start commands to game modules
    for topic, command in start_commands.items():
        client.publish(topic, json.dumps(command))
        time.sleep(0.1)
    
    # Start the timer on display module
    start_game_timer(client, timer_settings.get("game_duration", 300))
       
    print("🎮 All games started!")

def stop_all_games(client):
    """Stop all games"""
    print("⏹️ Stopping all games...")
   
    stop_command = {"type": "STOP_GAME", "reason": "manual_stop"}
    
    # Stop all modules
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(stop_command))
       
    print("All games stopped!")

def reset_all_games(client):
    """Reset all games to initial state"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    print("🔄 Resetting all games...")
   
    # Reset completion tracking
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
   
    # Send reset commands
    reset_commands = {
        "rpi/to/esp": {"type": "RESET_GAME"},
        "rpi/to/esp2": {"type": "RESET_X"},
        "rpi/to/esp3": {"type": "RESET_GAME"},
        "rpi/to/esp4": {"type": "RESET_GAME"}
    }
   
    for topic, command in reset_commands.items():
        client.publish(topic, json.dumps(command))
       
    print("All games reset!")

def main():
    # Initialize MQTT client
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
   
    # Load initial config
    config = load_config()
    print("Initial config loaded:", json.dumps(config, indent=2))
   
    print("\n🎮 Available commands:")
    print("- start_all_games()")
    print("- stop_all_games()")
    print("- reset_all_games()")
    print("- start_game_timer(duration=300)")
    print("- send_x_to_display(reason='TEST')")
   
    try:
        # Connect and start
        client.connect(BROKER, 1883, 60)
        print(f"🔌 Connecting to MQTT broker at {BROKER}:1883")
       
        # Make functions available globally for easy access
        import builtins
        builtins.start_all_games = lambda: start_all_games(client)
        builtins.stop_all_games = lambda: stop_all_games(client)
        builtins.reset_all_games = lambda: reset_all_games(client)
        builtins.start_timer = lambda duration=300: start_game_timer(client, duration)
        builtins.add_x = lambda reason="MANUAL": send_x_to_display(client, reason)
       
        client.loop_forever()
       
    except KeyboardInterrupt:
        print("\n👋 Shutting down...")
        stop_all_games(client)
        client.disconnect()
    except Exception as e:
        print(f"❌ Connection error: {e}")

if __name__ == "__main__":
    main()