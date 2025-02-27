# openhab
C++ interface to the OpenHab API, for ESP32

This interface is designed to implement OpenHab control elements, for instance wall panels. It will query an OpenHab instance to obtain data from a sitemap and allow constructing objects to represent the OpenHab items.

The intention is to subclass OpenHabObject using a custom object factory created by subclassing OpenHabFactory. My OpenHab light switch project illustrates this for the case of lvgl, but these classes are not tied to lvgl and don't use anything from it.
