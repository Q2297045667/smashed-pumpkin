//! Local, opt-in telemetry. Never loads, generates or saves chunks.
use pumpkin_plugin_api::{
    Context, Plugin, PluginMetadata, Server, permissions, register_plugin, scheduler::SchedulerExt,
    text::TextComponent, uuid,
};
use serde_json::{Value, json};
use std::collections::{BTreeSet, HashMap};
use std::{
    fs,
    io::Read,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{SystemTime, UNIX_EPOCH},
};

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn read_request() -> Option<Value> {
    let file = fs::File::open("data/request.json").ok()?;
    let mut bytes = Vec::new();
    file.take(8193).read_to_end(&mut bytes).ok()?;
    if bytes.len() > 8192 {
        return None;
    }
    let request: Value = serde_json::from_slice(&bytes).ok()?;
    let expiry = request["expires_at_ms"].as_u64()?;
    let now = now_ms();
    if request["version"] != 1 || expiry < now || expiry > now + 10_000 {
        return None;
    }
    uuid::parse(request["request"].as_str()?)?;
    Some(request)
}

#[derive(Default)]
struct State {
    last_action: String,
    action_result: Option<Value>,
    next_sample_ms: u64,
    refresh_hz: u64,
}

fn action_reason(action: &Value) -> Result<Option<&str>, &'static str> {
    let reason = match action.get("reason") {
        None | Some(Value::Null) => "",
        Some(Value::String(reason)) => reason.as_str(),
        _ => return Err("Invalid reason"),
    };
    if reason.len() > 512 || reason.chars().any(char::is_control) {
        return Err("Invalid reason");
    }
    let reason = reason.trim();
    Ok((!reason.is_empty()).then_some(reason))
}

fn action(server: &Server, request: &Value, state: &mut State) {
    let Some(action) = request.get("action") else {
        return;
    };
    let Some(id) = action["id"].as_str() else {
        return;
    };
    if uuid::parse(id).is_none() || id == state.last_action {
        return;
    }
    state.last_action = id.to_owned();
    let result = (|| -> Result<(), &str> {
        let player_id = uuid::parse(action["player"].as_str().ok_or("Invalid player")?)
            .ok_or("Invalid player")?;
        let player = server
            .get_player_by_uuid(player_id)
            .ok_or("Player is no longer online")?;
        if player.get_name() != action["name"].as_str().ok_or("Missing identity")? {
            return Err("Player identity changed");
        }
        let reason = action_reason(action)?;
        if now_ms() > request["expires_at_ms"].as_u64().unwrap_or(0) {
            return Err("Request expired");
        }
        match action["type"].as_str() {
            Some("kick") => {
                use pumpkin_plugin_api::player::*;
                if let Some(java) = player.as_java() {
                    java.kick(JavaKickOptions {
                        reason: TextComponent::text(reason.unwrap_or("")),
                        log_to_console: true,
                        teardown_policy: SocketTeardownPolicy::Graceful,
                    });
                } else if let Some(bedrock) = player.as_bedrock() {
                    bedrock.kick(&BedrockKickOptions {
                        reason: BedrockDisconnectReason::Kicked,
                        message: reason.unwrap_or("").into(),
                        skip_message: reason.is_none(),
                        filtered_message: String::new(),
                        log_to_console: true,
                        teardown_policy: SocketTeardownPolicy::Graceful,
                    });
                } else {
                    return Err("Unsupported player connection");
                }
            }
            Some("ban") => {
                player.ban(pumpkin_plugin_api::player::BanPlayerOptions {
                    reason: reason.map(TextComponent::text),
                    source: Some("Smashed Pumpkin".into()),
                    expires_at_utc: None,
                    duration_seconds: None,
                    kick_if_online: true,
                    log_to_console: true,
                });
            }
            _ => return Err("Unsupported action"),
        }
        Ok(())
    })();
    state.action_result = Some(json!({"id": id, "ok": result.is_ok(),
        "message": result.err().unwrap_or("Action completed")}));
}

struct BusyGuard<'a>(&'a AtomicBool);
impl Drop for BusyGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
    }
}
fn snapshot(server: Server, state: &Arc<Mutex<State>>, busy: &AtomicBool) {
    if busy.swap(true, Ordering::Acquire) {
        return;
    }
    let _guard = BusyGuard(busy);
    let Some(request) = read_request() else {
        return;
    };
    let hz = request["refresh_hz"].as_u64().unwrap_or(2).clamp(2, 20);
    let sampled_at = now_ms();
    {
        let mut guard = state.lock().unwrap_or_else(|e| e.into_inner());
        if guard.refresh_hz == hz && sampled_at < guard.next_sample_ms {
            return;
        }
        let period = 1000 / hz;
        // Preserve phase so a 19 Hz setting does not round down to 10 Hz at 20 TPS.
        guard.next_sample_ms = if guard.refresh_hz != hz
            || sampled_at.saturating_sub(guard.next_sample_ms) > period * 2
        {
            sampled_at + period
        } else {
            guard.next_sample_ms + period
        };
        guard.refresh_hz = hz;
    }
    // Host calls can re-enter a plugin: never hold a lock across them.
    let mut local = {
        let guard = state.lock().unwrap_or_else(|e| e.into_inner());
        State {
            last_action: guard.last_action.clone(),
            action_result: guard.action_result.clone(),
            next_sample_ms: guard.next_sample_ms,
            refresh_hz: guard.refresh_hz,
        }
    };
    action(&server, &request, &mut local);
    let all_worlds = server.get_all_worlds();
    let all_players = server.get_all_players();
    let complete = all_worlds.len() <= 256 && all_players.len() <= 512;
    let mut loaded: HashMap<(String, String), BTreeSet<(i32, i32)>> = HashMap::new();
    let players: Vec<Value> = all_players
        .into_iter()
        .take(512)
        .map(|player| {
            let pos = player.get_position();
            let world = player.get_world();
            let name = world.get_name();
            let dimension = world.get_dimension();
            let chunk_x = (pos.0 / 16.0).floor() as i32;
            let chunk_z = (pos.2 / 16.0).floor() as i32;
            // The pinned host implementation only looks up loaded_chunks.
            // Never replace this with get_block_state or a chunk-loading API.
            if world.get_chunk(chunk_x, chunk_z).is_some() {
                loaded
                    .entry((name.clone(), dimension.clone()))
                    .or_default()
                    .insert((chunk_x, chunk_z));
            }
            json!({"id": uuid::to_string(player.get_id()), "name": player.get_name(),
            "world": name, "dimension": dimension,
            "x": pos.0, "y": pos.1, "z": pos.2, "yaw": player.get_yaw(), "ping": player.get_ping()})
        })
        .collect();
    let worlds: Vec<Value> = all_worlds
        .into_iter()
        .take(256)
        .map(|world| {
            let name = world.get_name();
            let dimension = world.get_dimension();
            let chunks: Vec<Value> = loaded
                .get(&(name.clone(), dimension.clone()))
                .into_iter()
                .flatten()
                .map(|&(x, z)| json!({"x": x, "z": z}))
                .collect();
            let border = world.get_world_border();
            json!({"name": name, "dimension": dimension, "loaded_chunks": chunks,
            "border": {"x": border.get_center_x(), "z": border.get_center_z(),
                "diameter": border.get_diameter(), "transition_known": false}})
        })
        .collect();
    let document = json!({"version": 1, "request": request["request"], "complete": complete,
        "updated_at_ms": now_ms(), "worlds": worlds, "players": players,
        "action_result": local.action_result});
    *state.lock().unwrap_or_else(|e| e.into_inner()) = local;
    if let Ok(bytes) = serde_json::to_vec(&document) {
        if bytes.len() <= 1024 * 1024 && fs::write("data/live.json.tmp", bytes).is_ok() {
            let _ = fs::rename("data/live.json.tmp", "data/live.json");
        }
    }
}

struct MapBridge;
impl Plugin for MapBridge {
    fn new() -> Self {
        Self
    }
    fn metadata(&self) -> PluginMetadata {
        PluginMetadata {
            name: "map-bridge".into(),
            version: env!("CARGO_PKG_VERSION").into(),
            authors: vec!["Smashed Pumpkin".into()],
            description: "Local opt-in minimap telemetry; no chunk generation or networking."
                .into(),
            dependencies: vec![],
            permissions: vec![permissions::FS_WRITE_DATA.into()],
        }
    }
    fn on_load(&self, context: Context) -> Result<(), String> {
        let state = Arc::new(Mutex::new(State::default()));
        let busy = AtomicBool::new(false);
        context.schedule_repeating_task(1, 1, move |server| snapshot(server, &state, &busy));
        Ok(())
    }
}
register_plugin!(MapBridge);

#[cfg(test)]
mod tests {
    use super::action_reason;
    use serde_json::json;

    #[test]
    fn missing_or_empty_reasons_do_not_invent_a_message() {
        for action in [
            json!({}),
            json!({"reason": null}),
            json!({"reason": ""}),
            json!({"reason": "   "}),
        ] {
            assert_eq!(action_reason(&action), Ok(None));
        }
    }

    #[test]
    fn supplied_reason_is_preserved_and_validated() {
        let action = json!({"reason": "  Repeated spam  "});
        assert_eq!(action_reason(&action), Ok(Some("Repeated spam")));
        for reason in [json!(42), json!("line\nbreak"), json!("x".repeat(513))] {
            assert_eq!(
                action_reason(&json!({"reason": reason})),
                Err("Invalid reason")
            );
        }
    }
}
