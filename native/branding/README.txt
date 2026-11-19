Shadow Logis identity
====================

Original vector artwork created for this project. The S remains the primary
shape; its small forward arrow suggests a shipment moving along a route.
mark.svg is dark ink on transparency for light surfaces. app-icon.svg uses
the same mark in silver on a midnight tile for the Dock and window icon.
These brand colors affect only the icon, not the desktop-following UI theme.

app-icon.png (1024px), app-icon.icns and app-icon.ico are generated only from
app-icon.svg. These exact files are allowed in source; no uploaded company logos
are included. Windows ICO includes 16, 24, 32, 48, 64, 128 and 256 pixel images.
The operating-system icon is separate from the company logo on dock receipts.
The app never replaces the user's company branding on shipping documents.

To regenerate, maintainers need rsvg-convert and Python with Pillow:
  python3 tools/build-icons.py

Normal Linux/Mac/Windows builds use the committed assets and need neither tool.
