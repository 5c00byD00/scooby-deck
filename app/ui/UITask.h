// Scooby Deck v3 — the MeshCore UITask hook.
// Upstream companion_radio/main.cpp includes "UITask.h" and does:
//   UITask ui_task(&board, &interface_manager);
//   ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
//   the_mesh.setListener(&ui_task);
// The shim pattern (our UITask replaces upstream ui-new, no MeshCore changes) follows Faro
// (radeksixi/sixi-tdeck-plus, Apache-2.0, app/ui/UITask.h). GPL-3.0-or-later.
#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include "AbstractUITask.h"
#include "NodePrefs.h"

class UITask : public AbstractUITask {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* interfaces)
    : AbstractUITask(board, interfaces) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // AbstractUITask
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                     uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details,
                            const char* text) override;
  void onQueueSizeChanged(int msgcount) override;
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len,
                           const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
};
