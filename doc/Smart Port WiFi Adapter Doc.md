# Rokenbok Smart Port WiFi Adapter Documentation

## Required Equipment

 1. Rokenbok WiFi Smart Port Adapter (SPA)
 2. USB A Male to USB B Male Cable (Printer Cable)
 3. Male to Male Mini-DIN-6 Cable (PS/2 Keyboard/Mouse Cable)

## Getting Started

 1. Power up the adapter by connecting it to a USB power source using the USB A to USB B cable.  You will see the red "PWR" LED illuminate.
 2. The blue "WIFI" LED on the device will begin blinking slowly.  This indicates that the device is in the WiFi provisioning mode and is functioning as a wireless access point.
 3. Using a WiFi enabled device, connect to the "Smart-Port-Provisioning" network.  The password is also "Smart-Port-Provisioning".
 4. Using a web browser, navigate to "1.2.3.4" to access the adapter's provisioning web server.  Follow the instructions on the page to input the SSID, password, and static IP information for your local network.
	 - WARNING: DO NOT set your admin password to a password you use for anything else.  This password is sent over the internet unencrypted if you use it later!  Safe enough for this little toy, but don't compromise your important passwords!
	 - Note: The IP address information is required to set up a known "static IP" address for your adapter.  This way your router's DHCP server won't change the adapter's SPA and leave you unable to connect.
 5. After double checking all of your info, click the "Submit" button.  You will see the device restart.  If the WiFi LED goes solid blue, this means that it has successfully connected to your network.
	 - If the WiFi LED rapidbly blinks this means that it cannot connect to your network.  It will eventually clear your inputted credentials, reboot, and then re-enter provisioning mode.  You can also hold the reset button to re-enter provisioning mode at any time.  Double check your WiFi credentials and try again.
 6. Now connect the adapter to your Gen 1 Rokenbok RC command deck Smart Port using the male to male mini-DIN-6 cable.  It is best to do this while the command deck is off.
 7. Power up the command deck and the "SP" (Smart Port) LED on your adapter should turn green.
	 - If the SP LED does not illuminate, check your connection.  Excessively long cables (greater than 6 feet) may not work.
 8. On a device connected to your network (WiFi or ethernet), open a web browser and navigate to the static IP address that you selected.  You are now ready to use the device!
 9. You can control Rokenbok vehicles using either your keyboard or a standard layout gamepad.  Non standard gamepads will require additional software to emulate a standard gamepad.  See the "Help" page for more information about keyboard controls.  Gamepads should be automatically detected when you press a button.  The controls are identical to normal Rokenbok, except that start and select are used to change your vehicle selection.
