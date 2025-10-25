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

module_last_seen = {
    "wire": 0,
    "display": 0,
    "maze": 0,
    "button": 0
}

module_missed_heartbeats = {
    "wire": 0,
    "display": 0,
    "maze": 0,
    "button": 0
}

activation_sent = False
games_paused = False

BROKER = "192.168.1.201"
CONFIG_FILE = "config.json"

HEARTBEAT_TIMEOUT = 10
HEARTBEAT_CHECK_INTERVAL = 2

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
    
    print("\n=== MODULE CONNECTION STATUS ===")
    for module, status in modules_connected.items():
        print(f"  {module.upper()}: {'CONNECTED' if status else 'WAITING...'}")
    print("================================\n")
    
    all_connected = all(modules_connected.values())
    
    if all_connected and not activation_sent:
        print("\n" + "="*50)
        print("ALL MODULES CONNECTED! SENDING ACTIVATION SIGNAL...")
        print("="*50 + "\n")
        
        config = load_config()
        game_duration = config.get("timer_settings", {}).get("game_duration", 360)
        
        activation_command = {
            "type": "ACTIVATE",
            "command": "ACTIVATE",
            "message": "All modules connected - system activated!",
            "duration": game_duration,
            "timestamp": int(time.time() * 1000)
        }
        
        topics = {
            "rpi/to/esp": "Wire Module",
            "rpi/to/esp2": "Display Module", 
            "rpi/to/esp3": "Maze Module",
            "rpi/to/esp4": "Button Module"
        }
        
        for topic, module_name in topics.items():
            result = client.publish(topic, json.dumps(activation_command))
            print(f"  SENT ACTIVATION to {module_name} ({topic})")
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
    print(f"⏱️  Game timer started: {duration} seconds")

def send_x_to_display(client, reason="GAME_FAILURE"):
    """Send X mark to display"""
    x_command = {
        "type": "X",
        "command": "X",
        "message": f"X mark added due to: {reason}"
    }
    
    client.publish("rpi/to/esp2", json.dumps(x_command))
    print(f"❌ X mark sent: {reason}")

# ========== RESET FUNCTIONS ==========

def reset_all_games(client):
    """Complete game reset - resets all modules to initial state"""
    global wire_game_completed, maze_game_completed, button_game_completed
    global activation_sent, games_paused
    
    print("\n" + "="*60)
    print("🔄 RESETTING ALL GAMES")
    print("="*60)
    
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
    activation_sent = False
    games_paused = False
    
    for module in module_missed_heartbeats:
        module_missed_heartbeats[module] = 0
    
    reset_command = {
        "type": "RESET_GAME",
        "command": "RESET_GAME",
        "message": "Complete game reset",
        "timestamp": int(time.time() * 1000)
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(reset_command))
        time.sleep(0.1)
    
    print("✅ Reset commands sent to all modules")
    print("   - Wire module: Game state reset")
    print("   - Display module: Timer stopped, X marks cleared")
    print("   - Maze module: Player position reset")
    print("   - Button module: Ready for new game")
    print("\n⏳ Waiting for modules to reconnect...")
    print("="*60 + "\n")

def quick_reset(client):
    """Quick reset - resets game state but keeps modules connected"""
    global wire_game_completed, maze_game_completed, button_game_completed
    
    print("\n" + "="*60)
    print("⚡ QUICK GAME RESET")
    print("="*60)
    
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
    
    reset_command = {
        "type": "RESET_GAME",
        "command": "RESET_GAME",
        "message": "Quick game reset",
        "keep_timer": True,
        "timestamp": int(time.time() * 1000)
    }
    
    game_topics = ["rpi/to/esp", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in game_topics:
        client.publish(topic, json.dumps(reset_command))
        time.sleep(0.1)
    
    x_reset_command = {
        "type": "RESET_X",
        "command": "RESET_X",
        "message": "X counter reset"
    }
    client.publish("rpi/to/esp2", json.dumps(x_reset_command))
    
    print("✅ Quick reset complete")
    print("   - Games reset")
    print("   - X marks cleared")
    print("   - Timer continues running")
    print("="*60 + "\n")

def restart_game(client, duration=None):
    """Full restart - reset everything and start new timer"""
    global wire_game_completed, maze_game_completed, button_game_completed
    
    if duration is None:
        config = load_config()
        duration = config.get("timer_settings", {}).get("game_duration", 300)
    
    print("\n" + "="*60)
    print(f"🔄 FULL RESTART - NEW {duration}s TIMER")
    print("="*60)
    
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
    
    # Stop current timer
    stop_command = {
        "type": "STOP_TIMER",
        "command": "STOP_TIMER",
        "message": "Stopping for restart"
    }
    client.publish("rpi/to/esp2", json.dumps(stop_command))
    time.sleep(0.5)
    
    # Reset all games
    reset_command = {
        "type": "RESET_GAME",
        "command": "RESET_GAME",
        "message": "Full game restart",
        "timestamp": int(time.time() * 1000)
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(reset_command))
        time.sleep(0.1)
    
    # Reset X counter
    x_reset_command = {
        "type": "RESET_X",
        "command": "RESET_X",
        "message": "X counter reset"
    }
    client.publish("rpi/to/esp2", json.dumps(x_reset_command))
    time.sleep(0.5)
    
    # Start new timer
    start_game_timer(client, duration)
    
    print(f"✅ Full restart complete")
    print(f"   - All games reset")
    print(f"   - X marks cleared")
    print(f"   - New timer started: {duration}s")
    print("="*60 + "\n")

def emergency_stop(client):
    """Emergency stop - pause everything immediately"""
    global games_paused
    
    print("\n" + "="*60)
    print("🚨 EMERGENCY STOP ACTIVATED")
    print("="*60)
    
    games_paused = True
    
    stop_command = {
        "type": "PAUSE_TIMER",
        "command": "PAUSE_TIMER",
        "reason": "emergency_stop",
        "message": "Emergency stop activated"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(stop_command))
        time.sleep(0.1)
    
    print("✅ All games paused")
    print("   Use resume() to continue")
    print("   Or reset_all_games() to start fresh")
    print("="*60 + "\n")

def resume(client):
    """Resume all games after pause"""
    global games_paused
    
    print("\n▶️  RESUMING ALL GAMES...")
    
    games_paused = False
    
    resume_command = {
        "type": "RESUME_TIMER",
        "command": "RESUME_TIMER",
        "reason": "manual_resume",
        "message": "Games resumed by operator"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(resume_command))
        time.sleep(0.05)
    
    print("✅ All games resumed\n")

def status():
    """Display current game status"""
    print("\n" + "="*60)
    print("CURRENT GAME STATUS")
    print("="*60)
    print(f"Wire Game: {'✅ Completed' if wire_game_completed else '⏳ In Progress'}")
    print(f"Maze Game: {'✅ Completed' if maze_game_completed else '⏳ In Progress'}")
    print(f"Button Game: {'✅ Completed' if button_game_completed else '⏳ In Progress'}")
    print()
    print("Module Connections:")
    for module, connected in modules_connected.items():
        status_icon = "🟢 Connected" if connected else "🔴 Disconnected"
        print(f"  {module.capitalize()}: {status_icon}")
    print()
    print(f"Game Paused: {'Yes' if games_paused else 'No'}")
    print(f"Activation Sent: {'Yes' if activation_sent else 'No'}")
    print("="*60 + "\n")

def set_timer(client, seconds):
    """Change timer duration"""
    config = load_config()
    config['timer_settings']['game_duration'] = seconds
    
    with open(CONFIG_FILE, 'w') as f:
        json.dump(config, f, indent=4)
    
    print(f"\n⏱️  Timer configuration updated to {seconds}s")
    print("   (Will apply on next game start)\n")

def set_button(client, button_id):
    """Switch to different button configuration"""
    config = load_config()
    
    print(f"\n🔘 Switching to {button_id}...")
    
    # Disable all buttons
    for btn_id in config['button_ID'].keys():
        config['button_ID'][btn_id]['enabled'] = False
    
    # Enable selected button
    if button_id in config['button_ID']:
        config['button_ID'][button_id]['enabled'] = True
        
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=4)
        
        # Send updated config to button module
        button_config = config['button_ID'][button_id]
        update_command = {
            "type": "UPDATE_BUTTON_CONFIG",
            "command": "UPDATE_BUTTON_CONFIG",
            "button_id": button_id,
            "target_time": button_config['target_time'],
            "buffer": button_config['buffer'],
            "enabled": True
        }
        client.publish("rpi/to/esp4", json.dumps(update_command))
        
        print(f"✅ Switched to {button_id}")
        print(f"   Target: {button_config['target_time']}ms ± {button_config['buffer']}ms\n")
    else:
        print(f"❌ Error: {button_id} not found in config\n")

def set_maze(client, maze_id):
    """Switch to different maze layout"""
    config = load_config()
    
    print(f"\n🎮 Switching to {maze_id}...")
    
    # Disable all mazes
    for m_id in config['maze_ID'].keys():
        config['maze_ID'][m_id]['enabled'] = False
    
    # Enable selected maze
    if maze_id in config['maze_ID']:
        config['maze_ID'][maze_id]['enabled'] = True
        
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=4)
        
        # Send updated config to maze module
        send_maze_config(client, config)
        
        print(f"✅ Switched to {maze_id}")
        print(f"   Name: {config['maze_ID'][maze_id]['name']}\n")
    else:
        print(f"❌ Error: {maze_id} not found in config\n")

# ========== END RESET FUNCTIONS ==========

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"\n✓ SUCCESSFULLY CONNECTED TO MQTT BROKER")
    else:
        print(f"\n✗ FAILED TO CONNECT TO MQTT BROKER (rc: {rc})")
        return
    
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
            print(f"  ✓ Subscribed to {topic}")
    
    print("\n⏳ Waiting for ESP32 modules to connect...")
    print("-" * 50)
    
    import threading
    heartbeat_thread = threading.Thread(target=check_heartbeats, args=(client,), daemon=True)
    heartbeat_thread.start()

def on_message(client, userdata, msg):
    global button_game_completed, wire_game_completed, maze_game_completed
    global modules_connected, module_last_seen
   
    topic = msg.topic
    message_payload = msg.payload.decode()
    
    current_time = time.time()
    if topic == "esp/to/rpi":
        module_last_seen["wire"] = current_time
    elif topic == "esp2/to/rpi":
        module_last_seen["display"] = current_time
    elif topic == "esp3/to/rpi":
        module_last_seen["maze"] = current_time
    elif topic == "esp4/to/rpi":
        module_last_seen["button"] = current_time
    
    print(f"\n[RAW] Topic: {topic}")
    print(f"[RAW] Payload: {message_payload}")
   
    if topic == "config/request":
        try:
            config = load_config()
            client.publish("config/response", json.dumps(config))
            print("Sent config response")
        except Exception as e:
            print(f"Error handling config request: {e}")
        return
       
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
            else:
                client.publish("config/ack", json.dumps({"status": "error", "message": "Failed to save config"}))
        except Exception as e:
            print(f"Error handling config update: {e}")
        return
   
    try:
        data = json.loads(message_payload)
        
        if not isinstance(data, dict):
            return
        
        msg_type = data.get("type", "")
        print(f"[PARSED] Type: {msg_type}")
        
        if msg_type == "HEARTBEAT":
            pass
        
        elif msg_type == "DISPLAY_CONNECTED":
            print("\n✓ DISPLAY MODULE CONNECTED")
            modules_connected["display"] = True
            check_all_modules_connected(client)
            
        elif msg_type == "WIRE_MODULE_CONNECTED":
            print("\n✓ WIRE MODULE CONNECTED")
            modules_connected["wire"] = True
            check_all_modules_connected(client)
            
        elif msg_type == "MAZE_MODULE_CONNECTED":
            print("\n✓ MAZE MODULE CONNECTED")
            modules_connected["maze"] = True
            config = load_config()
            send_maze_config(client, config)
            check_all_modules_connected(client)
        
        elif msg_type == "REQUEST_MAZE_CONFIG":
            config = load_config()
            send_maze_config(client, config)
            
        elif msg_type == "BUTTON_MODULE_CONNECTED":
            print("\n✓ BUTTON MODULE CONNECTED")
            modules_connected["button"] = True
            config = load_config()
            send_button_config(client, config)
            check_all_modules_connected(client)
        
        elif msg_type == "REQUEST_BUTTON_CONFIG":
            config = load_config()
            send_button_config(client, config)
        
        elif msg_type == "TIMER_FINISHED":
            print("\n⏰ TIMER FINISHED - GAME OVER!")
            handle_timer_finished(client)
            
        elif msg_type == "X_ADDED":
            print(f"❌ X mark added! Total: {data.get('x_count', 0)}/{data.get('max_x_count', 3)}")
           
        elif msg_type == "MAX_X_REACHED":
            print("🚨 MAXIMUM X COUNT REACHED - GAME OVER!")
            handle_timer_finished(client)
            
        elif msg_type == "WRONG_CUT_ALERT":
            print(f"\n🔴 WRONG WIRE CUT!")
            print(f"  Cut: {data.get('wrong_wire_cut', 'Unknown')} | Expected: {data.get('expected_wire', 'Unknown')}")
            send_x_to_display(client, "WRONG_WIRE_CUT")
           
        elif msg_type == "PUZZLE_COMPLETED":
            print("\n✅ WIRE GAME COMPLETED!")
            wire_game_completed = True
            check_all_games_completed(client)
            
        elif msg_type == "MAZE_COMPLETED":
            print("\n✅ MAZE COMPLETED!")
            maze_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "WALL_HIT":
            print("\n💥 WALL HIT IN MAZE!")
            send_x_to_display(client, "MAZE_WALL_HIT")
            
        elif msg_type == "BUTTON_GAME_WON":
            print("\n✅ BUTTON GAME WON!")
            print(f"  Duration: {data.get('press_duration', 0)}ms | Target: {data.get('target_time', 0)}ms")
            button_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "BUTTON_GAME_LOST":
            print("\n🔴 BUTTON GAME - WRONG TIMING!")
            print(f"  Duration: {data.get('press_duration', 0)}ms | Target: {data.get('target_time', 0)}ms")
            send_x_to_display(client, "BUTTON_GAME_LOST")
           
    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}")
    except Exception as e:
        print(f"Error processing message: {e}")

def handle_timer_finished(client):
    """Handle when timer finishes"""
    print("\n🎮 GAME OVER - TIME'S UP!")
    
    game_over_message = {
        "type": "GAME_OVER",
        "command": "GAME_OVER",
        "message": "Time's up! Game over!",
        "reason": "timer_finished"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(game_over_message))
    
    print("Game over signals sent!")

def check_all_games_completed(client):
    """Check if all games completed"""
    global wire_game_completed, maze_game_completed, button_game_completed
   
    if wire_game_completed and maze_game_completed and button_game_completed:
        print("\n" + "="*50)
        print("🎉 ALL GAMES COMPLETED! VICTORY! 🎉")
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

def check_heartbeats(client):
    """Monitor module heartbeats"""
    global modules_connected, module_last_seen, activation_sent, games_paused, module_missed_heartbeats
    
    while True:
        time.sleep(HEARTBEAT_CHECK_INTERVAL)
        
        if not activation_sent:
            continue
        
        current_time = time.time()
        any_disconnected = False
        any_reconnected = False
        
        for module, last_seen in module_last_seen.items():
            if last_seen == 0:
                continue
            
            time_since_last_seen = current_time - last_seen
            was_connected = modules_connected[module]
            
            if time_since_last_seen > HEARTBEAT_TIMEOUT:
                if module_missed_heartbeats[module] < 2:
                    module_missed_heartbeats[module] += 1
                
                if module_missed_heartbeats[module] >= 2 and was_connected:
                    print(f"\n🔴 {module.upper()} DISCONNECTED!")
                    modules_connected[module] = False
                    any_disconnected = True
            else:
                if module_missed_heartbeats[module] > 0:
                    module_missed_heartbeats[module] = 0
                
                if not was_connected:
                    print(f"\n🟢 {module.upper()} RECONNECTED!")
                    modules_connected[module] = True
                    any_reconnected = True
        
        if any_disconnected and not games_paused:
            pause_all_games(client)
            games_paused = True
        
        if any_reconnected and games_paused:
            if all(modules_connected.values()):
                resume_all_games(client)
                games_paused = False

def pause_all_games(client):
    """Pause all games"""
    pause_command = {
        "type": "PAUSE_TIMER",
        "command": "PAUSE_TIMER",
        "reason": "module_disconnected"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(pause_command))
    print("⏸️  Games paused")

def resume_all_games(client):
    """Resume all games"""
    resume_command = {
        "type": "RESUME_TIMER",
        "command": "RESUME_TIMER",
        "reason": "module_reconnected"
    }
    
    topics = ["rpi/to/esp", "rpi/to/esp2", "rpi/to/esp3", "rpi/to/esp4"]
    for topic in topics:
        client.publish(topic, json.dumps(resume_command))
    print("▶️  Games resumed")

def send_button_config(client, config):
    """Send button configuration"""
    try:
        button_configs = config.get("button_ID", {})
        
        if not button_configs:
            print("⚠️ No button config found")
            return
        
        enabled_button = None
        for button_id, button_data in button_configs.items():
            if button_data.get("enabled", False):
                enabled_button = {
                    "button_id": button_id,
                    "target_time": button_data.get("target_time", 2000),
                    "buffer": button_data.get("buffer", 500),
                    "enabled": True
                }
                break
        
        if not enabled_button:
            first_id = list(button_configs.keys())[0]
            enabled_button = {
                "button_id": first_id,
                "target_time": button_configs[first_id].get("target_time", 2000),
                "buffer": button_configs[first_id].get("buffer", 500),
                "enabled": True
            }
        
        config_command = {
            "type": "UPDATE_BUTTON_CONFIG",
            "command": "UPDATE_BUTTON_CONFIG",
            **enabled_button
        }
        client.publish("rpi/to/esp4", json.dumps(config_command))
        print(f"✅ Sent button config: {enabled_button['button_id']}")
    
    except Exception as e:
        print(f"❌ Error sending button config: {e}")

def send_maze_config(client, config):
    """Send maze configuration"""
    maze_configs = config.get("maze_ID", {})

    if not maze_configs:
        print("⚠️ No maze config found")
        return

    enabled_maze = None
    for maze_id, maze_data in maze_configs.items():
        if maze_data.get("enabled", False):
                enabled_maze = maze_data.copy()
                enabled_maze["maze_id"] = maze_id
                break
        
        if not enabled_maze:
            first_id = list(maze_configs.keys())[0]
            enabled_maze = maze_configs[first_id].copy()
            enabled_maze["maze_id"] = first_id
        
        config_command = {
            "type": "UPDATE_MAZE_CONFIG",
            "command": "UPDATE_MAZE_CONFIG",
            **enabled_maze
        }
        
        json_str = json.dumps(config_command)
        client.publish("rpi/to/esp3", json_str)
        print(f"✅ Sent maze config: {enabled_maze['maze_id']}")