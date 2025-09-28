import paho.mqtt.client as mqtt
import json
import os

# Track completion status of all games
wire_game_completed = False
maze_game_completed = False
button_game_completed = False
BROKER = "192.168.1.201"
CONFIG_FILE = "config.json"

def create_default_config():
    """Create a default config file if it doesn't exist"""
    default_config = {
        "timer_settings": {
            "game_duration": 300,
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
    print(f"Received message on topic {topic}: {message_payload}")
   
    # Handle configuration requests
    if topic == "config/request":
        try:
            config = load_config()
            client.publish("config/response", json.dumps(config))
            print("Sent config response:", config)
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
                        "command": "UPDATE_SETTINGS",
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
       
        # Handle messages from first ESP32 (wire game)
        if isinstance(data, dict) and data.get("type") == "WRONG_CUT_ALERT":
            print(f" WRONG CUT ALERT! ")
            print(f"Player cut {data['wrong_wire_cut']} wire")
            print(f"Expected {data['expected_wire']} wire")
            print(f"Step: {data['current_step']}/{data['total_steps']}")
            # Send "X" signal to second ESP32
            client.publish("rpi/to/esp2", json.dumps({"command": "X"}))
            print("Sent 'X' signal to second ESP32 on topic 'rpi/to/esp2'")
           
        elif isinstance(data, dict) and data.get("type") == "PUZZLE_COMPLETED":
            print(" Wire game completed by first ESP32! ")
            wire_game_completed = True
            # Send STOP_TIMER command to second ESP32
            client.publish("rpi/to/esp2", json.dumps({"command": "STOP_TIMER"}))
            print("Sent 'STOP_TIMER' signal to second ESP32 on 'rpi/to/esp2'")
            check_all_games_completed(client)
           
        # Handle messages from third ESP32 (maze game)
        elif isinstance(data, dict) and data.get("type") == "MAZE_COMPLETED":
            print(" MAZE COMPLETED! ")
            print(f"Player finished the maze!")
            maze_game_completed = True
            check_all_games_completed(client)
           
        elif isinstance(data, dict) and data.get("type") == "WALL_HIT":
            print(f" WALL HIT IN MAZE! ")
            print(f"Player hit a wall and returned to start")
            # Send "X" signal to another ESP32 when wall is hit
            client.publish("rpi/to/esp2", json.dumps({"command": "X"}))  # Send to timer ESP32
            print("Sent 'X' signal to second ESP32 due to wall hit")
           
        elif isinstance(data, dict) and data.get("type") == "GAME_RESTART":
            print(" Maze game restarted by player ")
           
        # Handle messages from fourth ESP32 (button game)
        elif isinstance(data, dict) and data.get("type") == "BUTTON_GAME_WON":
            print(" BUTTON GAME WON! ")
            print(f"Player won the button timing game!")
            print(f"Press duration: {data['press_duration']}ms")
            print(f"Target time: {data['target_time']}ms")
            print(f"Difference: {data['difference']}ms")
            button_game_completed = True
            check_all_games_completed(client)
           
        elif isinstance(data, dict) and data.get("type") == "BUTTON_GAME_LOST":
            print(" BUTTON GAME LOST! ")
            print(f"Player lost the button timing game!")
            print(f"Press duration: {data['press_duration']}ms")
            print(f"Target time: {data['target_time']}ms")
            print(f"Difference: {data['difference']}ms")
           
            # Send "X" signal to other ESP32s when button game is lost
            client.publish("rpi/to/esp2", json.dumps({"command": "X"}))  # Send to timer ESP32
            print("Sent 'X' signal to timer ESP32 due to button game loss")
           
        # Handle messages from ESP32 Display module
        elif isinstance(data, dict) and data.get("type") == "DISPLAY_CONNECTED":
            print("📺 Display module connected!")
            print(f"Device: {data.get('device', 'Unknown')}")
            print(f"X Count: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
           
        elif isinstance(data, dict) and data.get("type") == "X_ADDED":
            print(f"❌ X mark added to display! Total: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
           
        elif isinstance(data, dict) and data.get("type") == "MAX_X_REACHED":
            print("🚨 DISPLAY: Maximum X count reached!")
            print(f"X Count: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
           
        elif isinstance(data, dict) and data.get("type") == "X_RESET":
            print("🔄 Display X counter reset")
           
        elif isinstance(data, dict) and data.get("type") == "TIMER_STARTED":
            print(f"⏰ Display timer started: {data.get('message', 'Unknown duration')}")
           
        elif isinstance(data, dict) and data.get("type") == "TIMER_STOPPED":
            print(f"⏹️ Display timer stopped: {data.get('message', 'Unknown reason')}")
           
        elif isinstance(data, dict) and data.get("type") == "TIMER_FINISHED":
            print("⏰ Display timer finished!")
            # Timer finished - this could trigger game over logic
            handle_timer_finished(client)
           
    except json.JSONDecodeError:
        # Handle non-JSON messages (e.g., "1" from second ESP32)
        message = message_payload.strip()
       
        if message == "1":
            print("Received '1' signal from second ESP32")
            # Send "X" signal to first ESP32 to deactivate wire game
            client.publish("rpi/to/esp", json.dumps({"command": "X"}))
            print("Sent 'X' signal to first ESP32 on topic 'rpi/to/esp'")
           
        else:
            print(f"Ignored non-JSON message: {message}")

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
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4", "rpi/to/display"]
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
            "timestamp": str(int(time.time()))
        }
       
        # Send to all ESP32s including display
        topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4", "rpi/to/display"]
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
   
    # Send start commands to all ESP32s including display
    commands = {
        "rpi/to/esp": {"command": "START_GAME", "reset": True},
        "rpi/to/esp2": {
            "command": "START_TIMER",
            "settings": timer_settings
        },
        "rpi/to/esp3": {"command": "START_GAME", "reset_position": True},
        "rpi/to/esp4": {"command": "START_GAME", "new_round": True},
        "rpi/to/display": {
            "type": "START_TIMER",
            "duration": timer_settings.get("game_duration", 300)
        }
    }
   
    # Send countdown signal first
    sync_start = {
        "type": "SYNCHRONIZED_START",
        "countdown": 3
    }
   
    for topic in commands.keys():
        client.publish(topic, json.dumps(sync_start))
   
    print("Countdown started...")
    import time
    time.sleep(3)
   
    # Send actual start commands
    for topic, command in commands.items():
        client.publish(topic, json.dumps(command))
        time.sleep(0.1)
       
    print("🎮 All games started!")

def stop_all_games(client):
    """Stop all games"""
    print("Stopping all games...")
   
    stop_command = {"command": "STOP_GAME", "reason": "manual_stop"}
    display_stop_command = {"type": "STOP_TIMER", "reason": "manual_stop"}
    
    # Stop all game modules
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(stop_command))
    
    # Stop display module
    client.publish("rpi/to/display", json.dumps(display_stop_command))
       
    print("All games stopped!")

def reset_all_games(client):
    """Reset all games to initial state"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    print("Resetting all games...")
   
    # Reset completion tracking
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
   
    # Send reset commands
    reset_commands = {
        "rpi/to/esp": {"command": "RESET_GAME"},
        "rpi/to/esp2": {"command": "RESET_TIMER"},
        "rpi/to/esp3": {"command": "RESET_GAME"},
        "rpi/to/esp4": {"command": "RESET_GAME"},
        "rpi/to/display": {"type": "RESET_X"}
    }
   
    for topic, command in reset_commands.items():
        client.publish(topic, json.dumps(command))
       
    print("All games reset!")

# Optional: Function to send commands to maze game
def send_maze_command(client, command):
    """Send commands to the maze game ESP32"""
    client.publish("rpi/to/esp3", json.dumps({"command": command}))
    print(f"Sent '{command}' to maze game")

# Optional: Function to send commands to button game
def send_button_game_command(client, command):
    """Send commands to the button game ESP32"""
    client.publish("rpi/to/esp4", json.dumps({"command": command}))
    print(f"Sent '{command}' to button game")

# Display control functions
def send_display_command(client, command_type, **kwargs):
    """Send commands to the display ESP32"""
    message = {"type": command_type}
    message.update(kwargs)
    client.publish("rpi/to/display", json.dumps(message))
    print(f"Sent '{command_type}' to display module")

def add_x_to_display(client):
    """Add an X mark to the display"""
    send_display_command(client, "X")

def reset_display_x(client):
    """Reset the X counter on the display"""
    send_display_command(client, "RESET_X")

def start_display_timer(client, duration=300):
    """Start the display timer"""
    send_display_command(client, "START_TIMER", duration=duration)

def stop_display_timer(client):
    """Stop the display timer"""
    send_display_command(client, "STOP_TIMER")

def test_display(client):
    """Test the display module"""
    send_display_command(client, "TEST")

def update_timer_settings(client, **settings):
    """Update timer settings in config and send to timer ESP32"""
    try:
        config = load_config()
       
        # Update timer settings
        if "timer_settings" not in config:
            config["timer_settings"] = {}
           
        config["timer_settings"].update(settings)
       
        if save_config(config):
            # Send updated settings to timer ESP32
            timer_update = {
                "command": "UPDATE_SETTINGS",
                "settings": config["timer_settings"]
            }
            client.publish("rpi/to/esp2", json.dumps(timer_update))
            print(f"Timer settings updated: {settings}")
            return True
        return False
    except Exception as e:
        print(f"Error updating timer settings: {e}")
        return False

def main():
    # Initialize MQTT client
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
   
    # Load initial config
    config = load_config()
    print("Initial config loaded:", json.dumps(config, indent=2))
   
    print("\n🎮 Available commands:")
    print("- start_all_games(client)")
    print("- stop_all_games(client)")
    print("- reset_all_games(client)")
    print("- update_timer_settings(client, game_duration=300, warning_time=60)")
    print("- send_maze_command(client, 'COMMAND')")
    print("- send_button_game_command(client, 'COMMAND')")
    print("\n📺 Display commands:")
    print("- add_x_to_display(client)")
    print("- reset_display_x(client)")
    print("- start_display_timer(client, duration=300)")
    print("- stop_display_timer(client)")
    print("- test_display(client)")
    print("- send_display_command(client, 'COMMAND_TYPE', **kwargs)")
   
    try:
        # Connect and start
        client.connect(BROKER, 1883, 60)
        print(f"🔌 Connecting to MQTT broker at {BROKER}:1883")
       
        # Make functions available globally for easy access
        import builtins
        builtins.start_all_games = lambda: start_all_games(client)
        builtins.stop_all_games = lambda: stop_all_games(client)
        builtins.reset_all_games = lambda: reset_all_games(client)
        builtins.update_timer = lambda **kwargs: update_timer_settings(client, **kwargs)
        builtins.add_x = lambda: add_x_to_display(client)
        builtins.reset_x = lambda: reset_display_x(client)
        builtins.start_timer = lambda duration=300: start_display_timer(client, duration)
        builtins.stop_timer = lambda: stop_display_timer(client)
        builtins.test_display = lambda: test_display(client)
       
        client.loop_forever()
       
    except KeyboardInterrupt:
        print("\n👋 Shutting down...")
        stop_all_games(client)
        client.disconnect()
    except Exception as e:
        print(f"❌ Connection error: {e}")

if __name__ == "__main__":
    main()