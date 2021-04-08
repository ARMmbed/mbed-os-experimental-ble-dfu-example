## Notes

If you're using Linux and the attribute structure of your device changes, you may have to delete the bluez cache of your device.

To do this, you must find the entry in `/var/lib/bluetooth` and delete the `cache` entry corresponding to your device's MAC address, eg: `sudo rm -rf /var/lib/bluetooth/9C\:B6\:D0\:BC\:F3\:E4/cache/F5:67:70:C3:BF:46`

**Then**, you must **also** restart the bluetooth systemd service to reload the cache (there may be other ways of forcing bluez to do this):

`sudo systemctl restart bluetooth`

Note that this will disconnect any bluetooth devices currently connected to your machine.
