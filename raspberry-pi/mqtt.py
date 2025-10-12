import paho.mqtt.client as mqtt
import json
import time
import threading
from game_config_manager import GameConfigManager  # Import simplified config manager

# Initialize config manager
config_manager = GameConfigManager("config.json")

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

HEARTBEAT_INTERVAL = 2
HEARTBEAT_TIMEOUT = 15
MAX_MISSED_HEARTBEATS = 2
HEARTBEAT_CHECK_INTERVAL = 2

def check_all_modules_connected(client):
    """Check if all modules are connected and send activation signal"""
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
        
        # Get game duration from config
        game_duration = config_manager.get_timer_duration()
        
        # Send activation signal to all modules
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
        
        # Send button configuration to button module
        send_button_config(client)
        
        activation_sent = True
        print(f"ALL ACTIVATION SIGNALS SENT WITH {game_duration}s TIMER!\n")
        return True
    
    return all_connected and activation_sent

def send_button_config(client, button_id=None):
    """Send button configuration to ESP32"""
    if button_id is None:
        # Get active button
        button_id, button_config = config_manager.get_active_button()
    else:
        button_config = config_manager.get_button_config(button_id)
    
    config_command = {
        "type": "UPDATE_BUTTON_CONFIG",
        "command": "UPDATE_BUTTON_CONFIG",
        "button_id": button_id,
        "target_time": button_config.get("target_time", 2000),
        "buffer": button_config.get("buffer", 500)
    }
    
    client.publish("rpi/to/esp4", json.dumps(config_command))
    print(f"📤 Sent button configuration: {button_id}")
    print(f"   Target: {button_config['target_time']}ms ± {button_config['buffer']}ms")

def start_game_timer(client, duration=None):
    """Start the game timer on the display module"""
    if duration is None:
        duration = config_manager.get_timer_duration()
    
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

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"\n✓ SUCCESSFULLY CONNECTED TO MQTT BROKER")
    else:
        print(f"\n✗ FAILED TO CONNECT TO MQTT BROKER (rc: {rc})")
        return
    
    # Subscribe to all topics
    topics = [
        ("esp/to/rpi", 0),
        ("esp2/to/rpi", 0),
        ("esp3/to/rpi", 0),
        ("esp4/to/rpi", 0)
    ]
    
    for topic, qos in topics:
        result, mid = client.subscribe(topic, qos)
        if result == mqtt.MQTT_ERR_SUCCESS:
            print(f"  ✓ Subscribed to {topic}")
    
    print("\n⏳ Waiting for ESP32 modules to connect...")
    print("-" * 50)
    
    # Start heartbeat checker thread
    heartbeat_thread = threading.Thread(target=check_heartbeats, args=(client,), daemon=True)
    heartbeat_thread.start()

def on_message(client, userdata, msg):
    global button_game_completed, wire_game_completed, maze_game_completed
    global modules_connected, module_last_seen
   
    topic = msg.topic
    message_payload = msg.payload.decode()
    
    # Update last seen timestamp
    current_time = time.time()
    if topic == "esp/to/rpi":
        module_last_seen["wire"] = current_time
    elif topic == "esp2/to/rpi":
        module_last_seen["display"] = current_time
    elif topic == "esp3/to/rpi":
        module_last_seen["maze"] = current_time
    elif topic == "esp4/to/rpi":
        module_last_seen["button"] = current_time
    
    # Silently handle heartbeats
    try:
        data = json.loads(message_payload)
        if isinstance(data, dict) and data.get("type") == "HEARTBEAT":
            return
    except:
        pass
    
    print(f"\n📨 [{topic}] {message_payload[:100]}...")
   
    try:
        data = json.loads(message_payload)
        
        if not isinstance(data, dict):
            return
        
        msg_type = data.get("type", "")
        
        # === CONNECTION STATUS HANDLING ===
        if msg_type == "DISPLAY_CONNECTED":
            print("✓ DISPLAY MODULE CONNECTED")
            modules_connected["display"] = True
            module_missed_heartbeats["display"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "WIRE_MODULE_CONNECTED":
            print("✓ WIRE MODULE CONNECTED")
            modules_connected["wire"] = True
            module_missed_heartbeats["wire"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "MAZE_MODULE_CONNECTED":
            print("✓ MAZE MODULE CONNECTED")
            modules_connected["maze"] = True
            module_missed_heartbeats["maze"] = 0
            check_all_modules_connected(client)
            
        elif msg_type == "BUTTON_MODULE_CONNECTED":
            print("✓ BUTTON MODULE CONNECTED")
            button_id = data.get('button_id', 'button_1')
            print(f"   Current config: {button_id}")
            modules_connected["button"] = True
            module_missed_heartbeats["button"] = 0
            check_all_modules_connected(client)
        
        # === BUTTON CONFIG ===
        elif msg_type == "REQUEST_BUTTON_CONFIG":
            print("📨 Button module requested configuration")
            send_button_config(client)
        
        elif msg_type == "BUTTON_CONFIG_UPDATED":
            button_id = data.get('button_id', 'unknown')
            print(f"✓ Button configuration updated: {button_id}")
        
        # === TIMER EVENTS ===
        elif msg_type == "TIMER_FINISHED":
            print("\n⏰ TIMER FINISHED - GAME OVER!")
            handle_timer_finished(client)
        
        # === X MARKS ===
        elif msg_type == "X_ADDED":
            x_count = data.get('x_count', 0)
            max_x = data.get('max_x_count', 3)
            print(f"❌ X mark added! Total: {x_count}/{max_x}")
           
        elif msg_type == "MAX_X_REACHED":
            print("🚨 MAXIMUM X COUNT REACHED - GAME OVER!")
            handle_timer_finished(client)
        
        # === WIRE GAME EVENTS ===
        elif msg_type == "WRONG_CUT_ALERT":
            print(f"🔴 WRONG WIRE CUT!")
            wrong_wire = data.get('wrong_wire_cut', 'Unknown')
            expected_wire = data.get('expected_wire', 'Unknown')
            step = data.get('current_step', 0)
            total = data.get('total_steps', 0)
            
            print(f"   Cut: {wrong_wire} | Expected: {expected_wire} | Step: {step}/{total}")
            send_x_to_display(client, "WRONG_WIRE_CUT")
           
        elif msg_type == "PUZZLE_COMPLETED":
            print("✅ WIRE GAME COMPLETED!")
            wire_game_completed = True
            check_all_games_completed(client)
        
        # === MAZE GAME EVENTS ===
        elif msg_type == "MAZE_COMPLETED":
            print("✅ MAZE COMPLETED!")
            maze_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "WALL_HIT":
            print("💥 WALL HIT IN MAZE!")
            send_x_to_display(client, "MAZE_WALL_HIT")
        
        # === BUTTON GAME EVENTS ===
        elif msg_type == "BUTTON_GAME_WON":
            print("✅ BUTTON GAME WON!")
            press_dur = data.get('press_duration', 0)
            target = data.get('target_time', 0)
            diff = data.get('difference', 0)
            button_id = data.get('button_id', 'button_1')
            
            print(f"   Button: {button_id} | Duration: {press_dur}ms | Target: {target}ms | Diff: {diff}ms")
            button_game_completed = True
            check_all_games_completed(client)
           
        elif msg_type == "BUTTON_GAME_LOST":
            print("🔴 BUTTON GAME - WRONG TIMING!")
            press_dur = data.get('press_duration', 0)
            target = data.get('target_time', 0)
            diff = data.get('difference', 0)
            button_id = data.get('button_id', 'button_1')
            buffer = data.get('buffer', 500)
            
            print(f"   Button: {button_id} | Duration: {press_dur}ms | Target: {target}ms")
            print(f"   Difference: {diff}ms (Allowed: ±{buffer}ms)")
            send_x_to_display(client, "BUTTON_WRONG_TIMING")
           
    except json.JSONDecodeError as e:
        print(f"❌ JSON decode error: {e}")
    except Exception as e:
        print(f"❌ Error processing message: {e}")

def handle_timer_finished(client):
    """Handle when timer finishes"""
    global wire_game_completed, maze_game_completed, button_game_completed
    
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
    
    # Reset game states
    wire_game_completed = False
    maze_game_completed = False
    button_game_completed = False
    
    print("Game over signals sent to all modules!")

def check_all_games_completed(client):
    """Check if all games are completed"""
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
           
        print("Victory signals sent to all ESP32s!")
        
        # Reset game states
        wire_game_completed = False
        maze_game_completed = False
        button_game_completed = False

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
        time.sleep(0.05)
    
    print("⏸️  All games paused")

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
        time.sleep(0.05)
    
    print("▶️  All games resumed")

def check_heartbeats(client):
    """Monitor heartbeats"""
    global modules_connected, module_last_seen, activation_sent, games_paused, module_missed_heartbeats
    
    print(f"💓 Heartbeat monitor started (timeout: {HEARTBEAT_TIMEOUT}s, max missed: {MAX_MISSED_HEARTBEATS})\n")
    
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
                if module_missed_heartbeats[module] < MAX_MISSED_HEARTBEATS:
                    module_missed_heartbeats[module] += 1
                
                if module_missed_heartbeats[module] >= MAX_MISSED_HEARTBEATS and was_connected:
                    print(f"🔴 {module.upper()} DISCONNECTED!")
                    modules_connected[module] = False
                    any_disconnected = True
            else:
                if module_missed_heartbeats[module] > 0:
                    module_missed_heartbeats[module] = 0
                
                if not was_connected:
                    print(f"🟢 {module.upper()} RECONNECTED!")
                    modules_connected[module] = True
                    any_reconnected = True
        
        if any_disconnected and not games_paused:
            pause_all_games(client)
            games_paused = True
        
        if any_reconnected and games_paused:
            if all(modules_connected.values()):
                resume_all_games(client)
                games_paused = False

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    print("="*70)
    print("         ESCAPE ROOM GAME CONTROL SYSTEM")
    print("="*70)
    config_manager.print_config()
   
    try:
        client.connect(BROKER, 1883, 60)
        print(f"Connecting to MQTT broker at {BROKER}:1883\n")
        
        # Make functions available in console
        import builtins
        builtins.show_config = lambda: config_manager.print_config()
        builtins.reload_config = lambda: config_manager.reload_config()
        builtins.set_timer = lambda secs: config_manager.set_timer_duration(secs)
        builtins.change_button = lambda btn_id: send_button_config(client, btn_id)
        builtins.update_button = lambda btn_id, target, buf: config_manager.update_button_config(btn_id, target, buf)
        
        print("\n📋 AVAILABLE COMMANDS:")
        print("  show_config()              - Display current configuration")
        print("  reload_config()            - Reload config from file")
        print("  set_timer(seconds)         - Set game timer duration")
        print("  change_button('button_2')  - Switch to different button")
        print("  update_button('button_1', target=2500, buffer=600)")
        print("                             - Update button configuration")
        print()
       
        client.loop_forever()
       
    except KeyboardInterrupt:
        print("\n\n👋 Shutting down...")
        client.disconnect()
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()