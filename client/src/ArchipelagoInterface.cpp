#include "ArchipelagoInterface.h"
#include "apuuid.hpp"
#include "hooks.h"
#include <spdlog/spdlog.h>

#ifdef __EMSCRIPTEN__
#define DATAPACKAGE_CACHE "/settings/datapackage.json"
#define UUID_FILE "/settings/uuid"
#else
#define DATAPACKAGE_CACHE "datapackage.json" // TODO: place in %appdata%
#define UUID_FILE "uuid" // TODO: place in %appdata%
#endif

bool ap_sync_queued = false;
APClient* ap;
APClient::Version APClientVersion = { 0, 4, 9 };
extern ClientCore* Core;
extern Hooks* GameHooks;

BOOL CArchipelago::Initialise(std::string URI) {
	spdlog::info("ClientArchipelago::Intialise");

	std::string uuid = ap_get_uuid(UUID_FILE);
	if (ap != nullptr) {
		ap->reset();
	}

	ap = new APClient(uuid, "Dark Souls II", URI);

	ap_sync_queued = false;
	ap->set_socket_connected_handler([]() {
	});

	ap->set_socket_disconnected_handler([]() {
	});

	ap->set_slot_connected_handler([](const json& data) {
		spdlog::info("Slot connected successfully, reading slot data ... ");

		data.at("slot").get_to(Core->pSlotName);

		if (data.contains("options")) {
			
		}
	});

	ap->set_slot_disconnected_handler([]() {
		spdlog::info("Slot disconnected");
	});

	ap->set_slot_refused_handler([](const std::list<std::string>& errors) {
		for (const auto& error : errors) {
			spdlog::warn("Connection refused: {}", error);
		}
	});

	ap->set_room_info_handler([]() {
		std::list<std::string> tags;
		tags.push_back("DeathLink"); // TODO: change this to be based on options
		ap->ConnectSlot(Core->pSlotName, Core->pPassword, 3, tags, APClientVersion);
	});

	ap->set_items_received_handler([](const std::list<APClient::NetworkItem>& receivedItems) {

		if (!ap->is_data_package_valid()) {
			// NOTE: this should not happen since we ask for data package before connecting
			if (!ap_sync_queued) ap->Sync();
			ap_sync_queued = true;
			return;
		}

		for (auto const& i : receivedItems) {
			Core->itemsToGive.push_back(i.item);
		}

	});

	//TODO :   * you can still use `set_data_package` or `set_data_package_from_file` during migration to make use of the old cache

	ap->set_print_handler([](const std::string& msg) {
		spdlog::info(msg);
	});

	ap->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
		auto message = ap->render_json(msg, APClient::RenderFormat::TEXT);
		spdlog::info(message);
	});

	ap->set_bounced_handler([](const json& cmd) {
		GameHooks->killPlayer();
		spdlog::debug("Bad deathlink packet!");
	});
	return true;
}


VOID CArchipelago::say(std::string message) {
	if (ap && ap->get_state() == APClient::State::SLOT_CONNECTED) {
		ap->Say(message);
	}
}


BOOLEAN CArchipelago::isConnected() {
	return ap && ap->get_state() == APClient::State::SLOT_CONNECTED;
}

VOID CArchipelago::update() {
	if (ap) ap->poll();

	std::list<int64_t> locations = GameHooks->getLocations();
	if (isConnected() && locations.size() > 0 && ap->LocationChecks(locations)) {
		GameHooks->clearLocations(locations);
	};
}

VOID CArchipelago::gameFinished() {
	if (ap) ap->StatusUpdate(APClient::ClientStatus::GOAL);
}

VOID CArchipelago::sendDeathLink() {
	if (!isConnected()) return;

	spdlog::info("Sending deathlink");

	json data{
		{"time", ap->get_server_time()},
		{"cause", "Dark Souls II."},
		{"source", ap->get_slot()},
	};
	ap->Bounce(data, {}, {}, { "DeathLink" });
}