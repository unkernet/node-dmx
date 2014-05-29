{
  "targets": [
    {
      "target_name": "dmx_native",
      "sources": [ "dmx.cc" ],
      "libraries": [ "-lftdi" ]
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "dmx_native" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/dmx_native.node" ],
          "destination": "."
        }
      ]
    }
  ]
}
