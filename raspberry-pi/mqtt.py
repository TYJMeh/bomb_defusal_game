import paho.mqtt.client as mqtt
import json
import os
import time
import threading

# Track completion status of all games
wire_game_completed = False
maze_game_completed = False
button_game_completed = False

# Track module connections with better tracking
modules_connected = {
    "wire": False,
    "display": False,
    "maze": False,
    "button": False
}

# Track last heartbeat from each module
module_last_seen = {
    "wire": 0,
    "display": 0,
    "maze": 0,
    "button": 0
}

# Track missed heartbeats
module_missed_heartbeats = {
    "wire": 0,
    "display": 0,
    "maze": 0,
    "button": 0
}

# Track if activation has been sent
activation_sent = False
games_paused = False

BROKER = "192.168.1.201"
CONFIG_FILE = "config.json"

# Heartbeat settings
HEARTBEAT_INTERVAL = 2  # ESP32s send every 3 seconds
HEARTBEAT_TIMEOUT = 15   # Consider missed if not received within 15 seconds
MAX_MISSED_HEARTBEATS = 2  # Disconnect after 2 missed (16 seconds total)
HEARTBEAT_CHECK_INTERVAL = 2  # Check every 2 seconds

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
    global modules_connected, activation_sent
    
    # Print current status for debugging
    print("\n=== MODULE CONNECTION STATUS ===")
    for module, status in modules_connected.items():
        print(f"  {module.upper()}: {'CONNECTED' if status else 'WAITING...'}")
    print("================================\n")
    
    all_connected = all(modules_connected.values())
    
    if all_connected and not activation_sent:
        print("\n" + "="*50)
        print("ALL MODULES CONNECTED! SENDING ACTIVATION SIGNAL...")
        print("="*50 + "\n")
        
        # Load config to get timer duration
        config = load_config()
        game_duration = config.get("timer_settings", {}).get("game_duration", 360)
        
        # Send activation signal to all modules
        activation_command = {
            "type": "ACTIVATE",
            "command": "ACTIVATE",
            "message": "All modules connected - system activated!",
            "duration": game_duration,
            "timestamp": int(time.time() * 1000)
        }
        
        # Send to all modules with their specific topics
        topics = {
            "rpi/to/esp": "Wire Module",
            "rpi/to/esp2": "Display Module", 
            "rpi/to/esp3": "Maze Module",
            "rpi/to/esp4": "Button Module"
        }
        
        for topic, module_name in topics.items():
            result = client.publish(topic, json.dumps(activation_command))
            print(f"  SENT ACTIVATION to {module_name} ({topic})")
            print(f"    Result: {'SUCCESS' if result.rc == mqtt.MQTT_ERR_SUCCESS else 'FAILED'}")
            time.sleep(0.1)
        
        activation_sent = True
        print(f"\nALL ACTIVATION SIGNALS SENT WITH {game_duration}s TIMER!\n")
        return True
    elif all_connected and activation_sent:
        return True
    else:
        missing_modules = [module for module, connected in modules_connected.items() if not connected]
        print(f"WAITING FOR: {', '.join([m.upper() for m in missing_modules])}")
        return False

def start_game_timer(client, duration=300):
    """Start the game timer on the display module"""
    timer_command = {
        "type": "START_TIMER",
        "command": "START_TIMER",
        "duration": duration,
        "message": f"Game timer started for {duration} seconds"
    }
    
    client.publish("rpi/to/esp2", json.dumps(timer_command))
    print(f"Game timer started: {duration} seconds")

def send_x_to_display(client, reason="GAME_FAILURE"):
    """Send X mark to display"""
    x_command = {
        "type": "X",
        "command": "X",
        "message": f"X mark added due to: {reason}"
    }
    
    client.publish("rpi/to/esp2", json.dumps(x_command))
    print(f"X mark sent to display: {reason}")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"\nSUCCESSFULLY CONNECTED TO MQTT BROKER")
        print(f"Connection result code: {rc}")
    else:
        print(f"\nFAILED TO CONNECT TO MQTT BROKER")
        print(f"Connection result code: {rc}")
        return
    
    # Subscribe to all topics
    topics = [
        ("esp/to/rpi", 0),
        ("esp2/to/rpi", 0),
        ("esp3/to/rpi", 0),
        ("esp4/to/rpi", 0),
        ("config/request", 0),
        ("config/update", 0)
    ]
    
    for topic, qos in topics:
        result, mid = client.subscribe(topic, qos)
        if result == mqtt.MQTT_ERR_SUCCESS:
            print(f"  SUBSCRIBED to {topic}")
        else:
            print(f"  FAILED to subscribe to {topic}")
    
    print("\nWaiting for ESP32 modules to connect...")
    print("-" * 50)
    
    # Start heartbeat checker thread
    heartbeat_thread = threading.Thread(target=check_heartbeats, args=(client,), daemon=True)
    heartbeat_thread.start()

def on_message(client, userdata, msg):
    global button_game_completed, wire_game_completed, maze_game_completed
    global modules_connected, module_last_seen
   
    topic = msg.topic
    message_payload = msg.payload.decode()
    
    # Update last seen timestamp for the module
    current_time = time.time()
    if topic == "esp/to/rpi":
        module_last_seen["wire"] = current_time
    elif topic == "esp2/to/rpi":
        module_last_seen["display"] = current_time
    elif topic == "esp3/to/rpi":
        module_last_seen["maze"] = current_time
    elif topic == "esp4/to/rpi":
        module_last_seen["button"] = current_time
    
    # Don't print heartbeats to reduce spam
    try:
        data = json.loads(message_payload)
        if isinstance(data, dict) and data.get("type") == "HEARTBEAT":
            return  # Silently handle heartbeat
    except:
        pass
    
    # Debug: Print non-heartbeat messages
    print(f"\n[RAW] Topic: {topic}")
    print(f"[RAW] Payload: {message_payload}")
   
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
               
                if "timer_settings" in new_config:
                    timer_update = {
                        "type": "UPDATE_SETTINGS",
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
        data = json.loads(message_payload)
        
        if not isinstance(data, dict):
            print(f"[PARSED] Non-dict JSON: {data} (type: {type(data).__name__})")
            return
        
        msg_type = data.get("type", "")
        print(f"[PARSED] Type: {msg_type}")
        
        # === CONNECTION STATUS HANDLING ===
        if msg_type == "DISPLAY_CONNECTED":
            print("\nDISPLAY MODULE CONNECTED!")
            print(f"  Device: {data.get('device', 'Unknown')}")
            modules_connected["display"] = True
            module_missed_heartbeats["display"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "WIRE_MODULE_CONNECTED":
            print("\nWIRE MODULE CONNECTED!")
            print(f"  Device: {data.get('device', 'Unknown')}")
            modules_connected["wire"] = True
            module_missed_heartbeats["wire"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "MAZE_MODULE_CONNECTED":
            print("\nMAZE MODULE CONNECTED!")
            print(f"  Device: {data.get('device', 'Unknown')}")
            modules_connected["maze"] = True
            module_missed_heartbeats["maze"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "BUTTON_MODULE_CONNECTED":
            print("\nBUTTON MODULE CONNECTED!")
            print(f"  Device: {data.get('device', 'Unknown')}")
            print(f"  Target time: {data.get('target_time', 2000)}ms")
            modules_connected["button"] = True
            module_missed_heartbeats["button"] = 0
            check_all_modules_connected(client)
        
        # === ACTIVATION ACKNOWLEDGMENT ===
        elif msg_type == "DISPLAY_ACTIVATED":
            print("Display module activated and ready")
            
        elif msg_type == "MODULE_ACTIVATED":
            module_name = data.get('device', 'Unknown')
            print(f"{module_name} activated and ready")
        
        # === RECONNECTION ACKNOWLEDGMENTS ===
        elif msg_type in ["WIRE_GAME_PAUSED", "MAZE_PAUSED", "TIMER_PAUSED"]:
            print(f"{msg_type}: {data.get('message', '')}")
            
        elif msg_type in ["WIRE_GAME_RESUMED", "MAZE_RESUMED", "TIMER_RESUMED"]:
            print(f"{msg_type}: {data.get('message', '')}")
        
        # === GAME EVENT HANDLING ===
        elif msg_type == "TIMER_FINISHED":
            print("\nTIMER FINISHED - GAME OVER!")
            handle_timer_finished(client)
            
        elif msg_type == "X_ADDED":
            print(f"X mark added to display! Total: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
           
        elif msg_type == "MAX_X_REACHED":
            print("MAXIMUM X COUNT REACHED - GAME OVER!")
            print(f"X Count: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
            handle_timer_finished(client)
           
        elif msg_type == "TIMER_STARTED":
            print(f"Display timer started: {data.get('message', 'Unknown duration')}")
           
        elif msg_type == "TIMER_STOPPED":
            print(f"Display timer stopped: {data.get('message', 'Unknown reason')}")
            
        elif msg_type == "WRONG_CUT_ALERT":
            print(f"\nWRONG WIRE CUT!")
            print(f"  Player cut: {data.get('wrong_wire_cut', 'Unknown')} wire")
            print(f"  Expected: {data.get('expected_wire', 'Unknown')} wire")
            print(f"  Step: {data.get('current_step', '?')}/{data.get('total_steps', '?')}")
            send_x_to_display(client, "WRONG_WIRE_CUT")
           
        elif msg_type == "PUZZLE_COMPLETED":
            print("\nWIRE GAME COMPLETED!")
            wire_game_completed = True
            check_all_games_completed(client)
            
        elif msg_type == "MAZE_COMPLETED":
            print("\nMAZE COMPLETED!")
            maze_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "WALL_HIT":
            print("\nWALL HIT IN MAZE!")
            print("Player returned to start")
            send_x_to_display(client, "MAZE_WALL_HIT")
           
        elif msg_type == "GAME_RESTART":
            print("Maze game restarted by player")
            
        elif msg_type == "BUTTON_GAME_WON":
            print("\nBUTTON GAME WON!")
            print(f"  Press duration: {data.get('press_duration', 0)}ms")
            print(f"  Target time: {data.get('target_time', 0)}ms")
            print(f"  Difference: {data.get('difference', 0)}ms")
            button_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "BUTTON_GAME_LOST":
            print("\nBUTTON GAME LOST!")
            print(f"  Press duration: {data.get('press_duration', 0)}ms")
            print(f"  Target time: {data.get('target_time', 0)}ms")
            print(f"  Difference: {data.get('difference', 0)}ms")
            send_x_to_display(client, "BUTTON_GAME_LOST")
           
    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}")
        print(f"Raw message: '{message_payload}'")
        
        message = message_payload.strip()
        if message == "1":
            print("Received '1' signal from ESP32")
            if topic == "esp2/to/rpi":
                print("  Signal from display module")
        else:
            print(f"Unknown non-JSON message: {message}")
    except Exception as e:
        print(f"Error processing message: {e}")
        import traceback
        traceback.print_exc()

def handle_timer_finished(client):
    """Handle when the display timer finishes - trigger game over"""
    print("\nGAME OVER - TIME'S UP!")
    
    game_over_message = {
        "type": "GAME_OVER",
        "command": "GAME_OVER",
        "message": "Time's up! Game over!",
        "reason": "timer_finished"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(game_over_message))
    
    print("Game over signals sent to all modules!")

def check_all_games_completed(client):
    """Check if all games are completed and send victory signal"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    if wire_game_completed and maze_game_completed and button_game_completed:
        print("\n" + "="*50)
        print("ALL GAMES COMPLETED! VICTORY!")
        print("="*50 + "\n")
       
        victory_message = {
            "type": "VICTORY",
            "command": "VICTORY",
            "message": "All puzzles completed!",
            "timestamp": int(time.time() * 1000)
        }
       
        topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
        for topic in topics:
            client.publish(topic, json.dumps(victory_message))
           
        print("Victory signals sent to all ESP32s!")

def pause_all_games(client):
    """Pause all games due to disconnection"""
    pause_command = {
        "type": "PAUSE_TIMER",
        "command": "PAUSE_TIMER",
        "reason": "module_disconnected"
    }
    
    # Send pause to ALL modules
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(pause_command))
        time.sleep(0.05)  # Small delay between messages
    
    print("All games and timer paused due to disconnection")

def resume_all_games(client):
    """Resume all games after reconnection"""
    resume_command = {
        "type": "RESUME_TIMER",
        "command": "RESUME_TIMER",
        "reason": "module_reconnected"
    }
    
    # Send resume to ALL modules
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(resume_command))
        time.sleep(0.05)  # Small delay between messages
    
    print("All games and timer resumed after reconnection")

def check_heartbeats(client):
    """Check if modules are still connected via heartbeat timeout"""
    global modules_connected, module_last_seen, activation_sent, games_paused, module_missed_heartbeats
    
    print(f"\nHeartbeat monitor started:")
    print(f"  - Checking every {HEARTBEAT_CHECK_INTERVAL}s")
    print(f"  - Timeout after {HEARTBEAT_TIMEOUT}s of no heartbeat")
    print(f"  - Disconnect after {MAX_MISSED_HEARTBEATS} missed heartbeats ({HEARTBEAT_TIMEOUT * MAX_MISSED_HEARTBEATS}s total)\n")
    
    while True:
        time.sleep(HEARTBEAT_CHECK_INTERVAL)
        
        if not activation_sent:
            continue  # Don't check until system is activated
        
        current_time = time.time()
        any_disconnected = False
        any_reconnected = False
        disconnected_modules = []
        reconnected_modules = []
        
        for module, last_seen in module_last_seen.items():
            if last_seen == 0:
                continue  # Module never connected
            
            time_since_last_seen = current_time - last_seen
            was_connected = modules_connected[module]
            
            # Check if heartbeat timeout exceeded
            if time_since_last_seen > HEARTBEAT_TIMEOUT:
                # Increment missed heartbeat counter
                if module_missed_heartbeats[module] < MAX_MISSED_HEARTBEATS:
                    module_missed_heartbeats[module] += 1
                    if module_missed_heartbeats[module] == 1:
                        print(f"⚠️  {module.upper()}: Heartbeat timeout ({time_since_last_seen:.1f}s) - Warning {module_missed_heartbeats[module]}/{MAX_MISSED_HEARTBEATS}")
                
                # Disconnect only after multiple missed heartbeats
                if module_missed_heartbeats[module] >= MAX_MISSED_HEARTBEATS and was_connected:
                    print(f"\n🔴 {module.upper()} MODULE DISCONNECTED!")
                    print(f"   Last seen: {time_since_last_seen:.1f} seconds ago")
                    print(f"   Missed {module_missed_heartbeats[module]} consecutive heartbeats")
                    modules_connected[module] = False
                    any_disconnected = True
                    disconnected_modules.append(module)
            
            # Heartbeat received within timeout - check for reconnection
            else:
                # Reset missed heartbeat counter
                if module_missed_heartbeats[module] > 0:
                    if module_missed_heartbeats[module] < MAX_MISSED_HEARTBEATS:
                        print(f"✅ {module.upper()}: Heartbeat recovered")
                    module_missed_heartbeats[module] = 0
                
                # Module reconnected
                if not was_connected:
                    print(f"\n🟢 {module.upper()} MODULE RECONNECTED!")
                    print(f"   Last seen: {time_since_last_seen:.1f} seconds ago")
                    modules_connected[module] = True
                    any_reconnected = True
                    reconnected_modules.append(module)
        
        # Pause games if any module disconnected
        if any_disconnected and not games_paused:
            print(f"\n⏸️  PAUSING ALL GAMES")
            print(f"   Disconnected: {', '.join([m.upper() for m in disconnected_modules])}")
            pause_all_games(client)
            games_paused = True
        
        # Resume games if all reconnected
        if any_reconnected and games_paused:
            all_reconnected = all(modules_connected.values())
            if all_reconnected:
                print(f"\n▶️  ALL MODULES RECONNECTED - RESUMING GAMES")
                print(f"   Reconnected: {', '.join([m.upper() for m in reconnected_modules])}")
                resume_all_games(client)
                games_paused = False
            else:
                still_missing = [m for m, connected in modules_connected.items() if not connected]
                print(f"   {', '.join([m.upper() for m in reconnected_modules])} back online")
                print(f"   Still waiting for: {', '.join([m.upper() for m in still_missing])}")

def start_all_games(client):
    """Start all games simultaneously"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    print("\nStarting all games simultaneously...")
   
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
   
    config = load_config()
    timer_settings = config.get("timer_settings", {
        "game_duration": 300,
        "warning_time": 60,
        "countdown_start": 10
    })
   
    start_commands = {
        "rpi/to/esp": {"type": "START_GAME", "command": "START_GAME", "reset": True},
        "rpi/to/esp3": {"type": "START_GAME", "command": "START_GAME", "reset_position": True},
        "rpi/to/esp4": {"type": "START_GAME", "command": "START_GAME", "new_round": True}
    }
   
    sync_start = {
        "type": "SYNCHRONIZED_START",
        "command": "SYNCHRONIZED_START",
        "countdown": 3
    }
   
    for topic in start_commands.keys():
        client.publish(topic, json.dumps(sync_start))
    client.publish("rpi/to/esp2", json.dumps(sync_start))
   
    print("Countdown started...")
    time.sleep(3)
   
    for topic, command in start_commands.items():
        client.publish(topic, json.dumps(command))
        time.sleep(0.1)
    
    start_game_timer(client, timer_settings.get("game_duration", 300))
       
    print("All games started!")

def stop_all_games(client):
    """Stop all games"""
    print("\nStopping all games...")
   
    stop_command = {
        "type": "STOP_GAME",
        "command": "STOP_GAME",
        "reason": "manual_stop"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(stop_command))
       
    print("All games stopped!")

def reset_all_games(client):
    """Reset all games to initial state"""
    global wire_game_completed, maze_game_completed, button_game_completed
    global activation_sent, games_paused, module_missed_heartbeats
   
    print("\nResetting all games...")
   
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
    activation_sent = False
    games_paused = False
    
    # Reset missed heartbeat counters
    for module in module_missed_heartbeats:
        module_missed_heartbeats[module] = 0
   
    reset_commands = {
        "rpi/to/esp": {"type": "RESET_GAME", "command": "RESET_GAME"},
        "rpi/to/esp2": {"type": "RESET_X", "command": "RESET_X"},
        "rpi/to/esp3": {"type": "RESET_GAME", "command": "RESET_GAME"},
        "rpi/to/esp4": {"type": "RESET_GAME", "command": "RESET_GAME"}
    }
   
    for topic, command in reset_commands.items():
        client.publish(topic, json.dumps(command))
       
    print("All games reset!")

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
   
    config = load_config()
    print("Initial config loaded:", json.dumps(config, indent=2))
   
    print("\nAvailable commands:")
    print("- start_all_games()")
    print("- stop_all_games()")
    print("- reset_all_games()")
    print("- start_timer(duration=300)")
    print("- add_x(reason='TEST')")
   
    try:
        client.connect(BROKER, 1883, 60)
        print(f"\nConnecting to MQTT broker at {BROKER}:1883")
       
        import builtins
        builtins.start_all_games = lambda: start_all_games(client)
        builtins.stop_all_games = lambda: stop_all_games(client)
        builtins.reset_all_games = lambda: reset_all_games(client)
        builtins.start_timer = lambda duration=300: start_game_timer(client, duration)
        builtins.add_x = lambda reason="MANUAL": send_x_to_display(client, reason)
       
        client.loop_forever()
       
    except KeyboardInterrupt:
        print("\nShutting down...")
        stop_all_games(client)
        client.disconnect()
    except Exception as e:
        print(f"Connection error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()