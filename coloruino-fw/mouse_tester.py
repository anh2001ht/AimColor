import socket
import time

# Arduino network configuration (placeholders; match your firmware)
ARDUINO_IP = "192.168.1.216"
ARDUINO_PORT = 5353

# UDP socket setup
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)

def send_command(cmd: str):
    """Send a single command string to the Arduino."""
    if not cmd.endswith(";"):
        cmd += ";"
    sock.sendto(cmd.encode(), (ARDUINO_IP, ARDUINO_PORT))
    print(f"→ Sent: {cmd.strip()}")
    time.sleep(0.05)  # small delay between commands

# ---------------------------
# Basic command wrappers
# ---------------------------

def move(x, y):
    """Move the mouse by (x, y)."""
    send_command(f"M{x},{y}")

def click_left():
    """Left mouse click."""
    send_command("L")

def move_and_click(x, y):
    """Move to position and click (front click)."""
    send_command(f"F{x},{y}")

def poke(x, y):
    """Move to (x,y), click, then move back."""
    send_command(f"P{x},{y}")

# ---------------------------
# Interactive test menu
# ---------------------------

def menu():
    print("\n=== Arduino Mouse Test ===")
    print("Available commands:")
    print("  1. Move 100 right, 50 down")
    print("  2. Click left")
    print("  3. Move + click (50,30)")
    print("  4. Poke (30,-30)")
    print("  5. Continuous circle test")
    print("  6. Custom command")
    print("  0. Exit")

    while True:
        choice = input("\nSelect an option: ").strip()
        if choice == "1":
            move(100, 50)
        elif choice == "2":
            click_left()
        elif choice == "3":
            move_and_click(50, 30)
        elif choice == "4":
            poke(30, -30)
        elif choice == "5":
            circle_test()
        elif choice == "6":
            custom = input("Enter custom command (e.g., M20,-40;L;): ")
            send_command(custom)
        elif choice == "0":
            print("Exiting...")
            break
        else:
            print("Invalid option.")

# ---------------------------
# Demo pattern test
# ---------------------------

def circle_test(radius=100, steps=36, delay=0.05):
    """Move the mouse in a small circular pattern."""
    import math
    print("Running circle test...")
    for i in range(steps):
        angle = 2 * math.pi * i / steps
        x = int(radius * math.cos(angle))
        y = int(radius * math.sin(angle))
        move(x, y)
        time.sleep(delay)
    move(-radius, 0)
    print("Circle test complete.")

# ---------------------------
# Run the test menu
# ---------------------------

if __name__ == "__main__":
    try:
        menu()
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        sock.close()
