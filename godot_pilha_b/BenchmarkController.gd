extends Spatial

# Configuracoes carregadas do shared/benchmark_config.json
var cfg = {
	"width": 800,
	"height": 480,
	"fullscreen": false,
	"vsync": false,
	"cam_orbit_radius": 28.0,
	"cam_orbit_height": 12.0,
	"cam_angular_vel": 0.45,
	"cam_fov": 60.0,
	"near_plane": 0.1,
	"far_plane": 200.0,
	"light_dir": Vector3(-0.57735, -0.57735, -0.57735),
	"kd_diffuse": 0.85,
	"ka_ambient": 0.15,
	"base_color": Vector3(0.25, 0.55, 0.90),
	"base_radius": 3.5,
	"radius_step": 0.22,
	"base_orbital_speed": 1.2,
	"speed_decay": 0.012,
	"self_rotation_speed": 0.8,
	"warmup_seconds": 3.0,
	"sample_duration_seconds": 15.0,
	"rss_sample_interval": 30
}

const MESH_FILES = [
	"planet_500.obj",
	"planet_2500.obj",
	"planet_10000.obj",
	"planet_40000.obj"
]

const ALL_STAGES = [
	# Modo 1: Escalonamento por Densidade Poligonal (1 planeta)
	{"mode_name": "mesh_density", "stage_id": "MD_500",   "total_triangles": 500,   "planet_count": 1, "mesh_index": 0},
	{"mode_name": "mesh_density", "stage_id": "MD_2500",  "total_triangles": 2500,  "planet_count": 1, "mesh_index": 1},
	{"mode_name": "mesh_density", "stage_id": "MD_10000", "total_triangles": 10000, "planet_count": 1, "mesh_index": 2},
	{"mode_name": "mesh_density", "stage_id": "MD_40000", "total_triangles": 40000, "planet_count": 1, "mesh_index": 3},
	# Modo 2: Escalonamento por Quantidade de Planetas (malha base de 500 triangulos)
	{"mode_name": "planet_count", "stage_id": "PC_500",   "total_triangles": 500,   "planet_count": 1,  "mesh_index": 0},
	{"mode_name": "planet_count", "stage_id": "PC_2500",  "total_triangles": 2500,  "planet_count": 5,  "mesh_index": 0},
	{"mode_name": "planet_count", "stage_id": "PC_10000", "total_triangles": 10000, "planet_count": 20, "mesh_index": 0},
	{"mode_name": "planet_count", "stage_id": "PC_40000", "total_triangles": 40000, "planet_count": 80, "mesh_index": 0}
]

var planet_meshes = []
var lambert_material: ShaderMaterial = null

var camera_node: Camera = null
var planets_root: Spatial = null
var hud_layer: CanvasLayer = null
var hud_panel: ColorRect = null
var hud_label: Label = null

# Estado dos planetas e nos da Scene Tree (Spatial Nodes)
var active_planet_states = []
var active_planet_nodes = []

# Controle de execucao e camera interativa
var current_stage_idx: int = 0
var benchmark_suite_running: bool = false
var in_warmup_phase: bool = true
var show_hud: bool = true
var mouse_dragging: bool = false

var interactive_cam_yaw: float = 0.6
var interactive_cam_pitch: float = 0.4
var interactive_cam_dist: float = 28.0

# Telemetria (Equacoes 3 e 4 do artigo)
var stage_start_usec: int = 0
var last_swap_usec: int = 0
var prev_frame_time_ms: float = 0.0
var cached_rss_mb: float = 0.0
var frame_counter: int = 0
var samples = []
var suite_summaries = []


func _ready() -> void:
	_load_shared_config()

	OS.vsync_enabled = cfg["vsync"]
	OS.window_fullscreen = cfg["fullscreen"]
	Engine.target_fps = 0

	_setup_environment_and_camera()
	_setup_lambert_shader()
	_load_all_meshes()
	_setup_hud()

	_activate_stage(0)


func _load_shared_config() -> void:
	var config_path = ProjectSettings.globalize_path("res://../shared/benchmark_config.json")
	var file = File.new()
	if not file.file_exists(config_path):
		print("[AVISO] benchmark_config.json nao encontrado em: ", config_path, ". Usando padroes.")
		return

	if file.open(config_path, File.READ) == OK:
		var text = file.get_as_text()
		file.close()
		var parsed = JSON.parse(text)
		if parsed.error == OK and typeof(parsed.result) == TYPE_DICTIONARY:
			var data = parsed.result
			if data.has("display"):
				cfg["width"] = int(data["display"].get("width", 800))
				cfg["height"] = int(data["display"].get("height", 480))
				cfg["fullscreen"] = bool(data["display"].get("fullscreen", false))
				cfg["vsync"] = bool(data["display"].get("vsync", false))
			if data.has("camera"):
				cfg["cam_orbit_radius"] = float(data["camera"].get("orbit_radius", 28.0))
				cfg["cam_orbit_height"] = float(data["camera"].get("orbit_height", 12.0))
				cfg["cam_angular_vel"] = float(data["camera"].get("angular_velocity_rad_s", 0.45))
				cfg["cam_fov"] = float(data["camera"].get("fov_degrees", 60.0))
				cfg["near_plane"] = float(data["camera"].get("near_plane", 0.1))
				cfg["far_plane"] = float(data["camera"].get("far_plane", 200.0))
			if data.has("lighting_lambert"):
				var ld = data["lighting_lambert"].get("light_direction", [-0.57735, -0.57735, -0.57735])
				cfg["light_dir"] = Vector3(float(ld[0]), float(ld[1]), float(ld[2]))
				cfg["kd_diffuse"] = float(data["lighting_lambert"].get("kd_diffuse", 0.85))
				cfg["ka_ambient"] = float(data["lighting_lambert"].get("ka_ambient", 0.15))
				var bc = data["lighting_lambert"].get("base_color_rgb", [0.25, 0.55, 0.90])
				cfg["base_color"] = Vector3(float(bc[0]), float(bc[1]), float(bc[2]))
			if data.has("orbit_layout"):
				cfg["base_radius"] = float(data["orbit_layout"].get("base_radius", 3.5))
				cfg["radius_step"] = float(data["orbit_layout"].get("radius_step", 0.22))
				cfg["base_orbital_speed"] = float(data["orbit_layout"].get("base_orbital_speed", 1.2))
				cfg["speed_decay"] = float(data["orbit_layout"].get("speed_decay", 0.012))
				cfg["self_rotation_speed"] = float(data["orbit_layout"].get("self_rotation_speed", 0.8))
			if data.has("protocol"):
				cfg["warmup_seconds"] = float(data["protocol"].get("warmup_seconds", 3.0))
				cfg["sample_duration_seconds"] = float(data["protocol"].get("sample_duration_seconds", 15.0))
				cfg["rss_sample_interval"] = int(data["protocol"].get("rss_sample_interval_frames", 30))

	interactive_cam_dist = cfg["cam_orbit_radius"]


func _setup_environment_and_camera() -> void:
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(10.0 / 255.0, 12.0 / 255.0, 22.0 / 255.0, 1.0)

	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	camera_node = Camera.new()
	camera_node.keep_aspect = Camera.KEEP_HEIGHT
	camera_node.fov = cfg["cam_fov"]
	camera_node.near = cfg["near_plane"]
	camera_node.far = cfg["far_plane"]
	add_child(camera_node)
	camera_node.make_current()

	planets_root = Spatial.new()
	planets_root.name = "PlanetsRoot"
	add_child(planets_root)


func _setup_lambert_shader() -> void:
	# Implementa exatamente a Equacao (2) do artigo: Id = kd * max(0, dot(N, L))
	var shader = Shader.new()
	shader.code = """
shader_type spatial;
render_mode unshaded, cull_back;

uniform vec3 u_light_dir = vec3(-0.57735, -0.57735, -0.57735);
uniform vec3 u_base_color = vec3(0.25, 0.55, 0.90);
uniform float u_kd = 0.85;
uniform float u_ka = 0.15;

varying vec3 frag_normal;

void vertex() {
	frag_normal = normalize((WORLD_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 N = normalize(frag_normal);
	vec3 L = normalize(-u_light_dir);
	float Id = u_kd * max(0.0, dot(N, L));
	ALBEDO = u_base_color * (u_ka + Id);
}
"""
	lambert_material = ShaderMaterial.new()
	lambert_material.shader = shader
	lambert_material.set_shader_param("u_light_dir", cfg["light_dir"])
	lambert_material.set_shader_param("u_base_color", cfg["base_color"])
	lambert_material.set_shader_param("u_kd", cfg["kd_diffuse"])
	lambert_material.set_shader_param("u_ka", cfg["ka_ambient"])


func _load_all_meshes() -> void:
	planet_meshes.clear()
	for fname in MESH_FILES:
		var res_path = "res://meshes/" + fname
		if ResourceLoader.exists(res_path):
			var mesh_res = ResourceLoader.load(res_path)
			planet_meshes.append(mesh_res)
		else:
			# Fallback caso o arquivo ainda nao tenha sido copiado para res://meshes/
			var ext_path = ProjectSettings.globalize_path("res://../shared/meshes/" + fname)
			print("[AVISO] Carregando malha OBJ diretamente do disco: ", ext_path)
			planet_meshes.append(_parse_obj_fallback(ext_path))


func _parse_obj_fallback(filepath: String) -> ArrayMesh:
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

	var verts = PoolVector3Array()
	var uvs = PoolVector2Array()
	var norms = PoolVector3Array()

	var f = File.new()
	if f.open(filepath, File.READ) == OK:
		while not f.eof_reached():
			var line = f.get_line().strip_edges()
			if line.begins_with("v "):
				var p = line.split(" ", false)
				verts.append(Vector3(float(p[1]), float(p[2]), float(p[3])))
			elif line.begins_with("vt "):
				var p = line.split(" ", false)
				uvs.append(Vector2(float(p[1]), float(p[2])))
			elif line.begins_with("vn "):
				var p = line.split(" ", false)
				norms.append(Vector3(float(p[1]), float(p[2]), float(p[3])))
			elif line.begins_with("f "):
				var p = line.split(" ", false)
				for k in [1, 2, 3]:
					var idx = int(p[k].split("/")[0]) - 1
					st.add_normal(norms[idx])
					st.add_uv(uvs[idx])
					st.add_vertex(verts[idx])
		f.close()

	return st.commit()


func _setup_hud() -> void:
	hud_layer = CanvasLayer.new()
	add_child(hud_layer)

	hud_panel = ColorRect.new()
	hud_panel.color = Color(0.0, 0.0, 0.0, 0.72)
	hud_panel.rect_position = Vector2(10, 10)
	hud_panel.rect_size = Vector2(470, 175)
	hud_layer.add_child(hud_panel)

	hud_label = Label.new()
	hud_label.rect_position = Vector2(20, 18)
	hud_label.rect_size = Vector2(450, 160)
	hud_layer.add_child(hud_label)


func _build_planet_orbits(planet_count: int) -> Array:
	var list = []
	for i in range(planet_count):
		var p = {}
		if i == 0:
			p["orbit_radius"] = 0.0
			p["orbital_speed"] = 0.0
			p["initial_phase"] = 0.0
			p["self_rotation_speed"] = cfg["self_rotation_speed"]
			p["scale"] = 2.2 if planet_count == 1 else 1.4
		else:
			p["orbit_radius"] = cfg["base_radius"] + float(i) * cfg["radius_step"]
			p["orbital_speed"] = max(0.15, cfg["base_orbital_speed"] - float(i) * cfg["speed_decay"])
			p["initial_phase"] = float(i) * 2.3999632
			p["self_rotation_speed"] = cfg["self_rotation_speed"] + float(i % 5) * 0.15
			p["scale"] = 0.55
		list.append(p)
	return list


func _activate_stage(stage_idx: int) -> void:
	current_stage_idx = stage_idx
	var st = ALL_STAGES[current_stage_idx]

	# Remove os Spatial Nodes anteriores da Scene Tree
	for child in planets_root.get_children():
		planets_root.remove_child(child)
		child.queue_free()

	active_planet_nodes.clear()
	active_planet_states = _build_planet_orbits(st["planet_count"])

	var target_mesh = planet_meshes[st["mesh_index"]]

	# Instancia a hierarquia de Spatial Nodes (MeshInstance) no Scene Tree da Godot
	for i in range(st["planet_count"]):
		var mi = MeshInstance.new()
		mi.name = "PlanetNode_" + str(i)
		mi.mesh = target_mesh
		mi.material_override = lambert_material
		var s = active_planet_states[i]["scale"]
		mi.scale = Vector3(s, s, s)
		planets_root.add_child(mi)
		active_planet_nodes.append(mi)

	# Reinicia os contadores de telemetria para o estagio
	samples.clear()
	stage_start_usec = OS.get_ticks_usec()
	last_swap_usec = stage_start_usec
	prev_frame_time_ms = 0.0
	cached_rss_mb = _read_process_rss_mb()
	frame_counter = 0
	in_warmup_phase = true


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.scancode == KEY_H:
			show_hud = not show_hud
			hud_panel.visible = show_hud
			hud_label.visible = show_hud

		if not benchmark_suite_running:
			if event.scancode == KEY_M:
				_activate_stage((current_stage_idx + 4) % 8)
			var mode_offset = 4 if current_stage_idx >= 4 else 0
			if event.scancode == KEY_1:
				_activate_stage(mode_offset + 0)
			elif event.scancode == KEY_2:
				_activate_stage(mode_offset + 1)
			elif event.scancode == KEY_3:
				_activate_stage(mode_offset + 2)
			elif event.scancode == KEY_4:
				_activate_stage(mode_offset + 3)
			elif event.scancode == KEY_B:
				benchmark_suite_running = true
				suite_summaries.clear()
				_activate_stage(0)
		else:
			if event.scancode == KEY_BACKSPACE:
				benchmark_suite_running = false

	if not benchmark_suite_running:
		if event is InputEventMouseButton:
			if event.button_index == BUTTON_LEFT or event.button_index == BUTTON_RIGHT:
				mouse_dragging = event.pressed
			elif event.button_index == BUTTON_WHEEL_UP and event.pressed:
				interactive_cam_dist = clamp(interactive_cam_dist - 2.0, 4.0, 80.0)
			elif event.button_index == BUTTON_WHEEL_DOWN and event.pressed:
				interactive_cam_dist = clamp(interactive_cam_dist + 2.0, 4.0, 80.0)
		elif event is InputEventMouseMotion and mouse_dragging:
			interactive_cam_yaw += event.relative.x * 0.008
			interactive_cam_pitch = clamp(interactive_cam_pitch + event.relative.y * 0.008, -1.4, 1.4)


func _process(_delta: float) -> void:
	var now_usec = OS.get_ticks_usec()
	var sim_time = float(now_usec) * 1e-6

	# Controles continuos de teclado no Modo Interativo
	if not benchmark_suite_running:
		if Input.is_key_pressed(KEY_LEFT) or Input.is_key_pressed(KEY_A):
			interactive_cam_yaw -= 0.02
		if Input.is_key_pressed(KEY_RIGHT) or Input.is_key_pressed(KEY_D):
			interactive_cam_yaw += 0.02
		if Input.is_key_pressed(KEY_UP) or Input.is_key_pressed(KEY_W):
			interactive_cam_pitch = clamp(interactive_cam_pitch + 0.02, -1.4, 1.4)
		if Input.is_key_pressed(KEY_DOWN) or Input.is_key_pressed(KEY_S):
			interactive_cam_pitch = clamp(interactive_cam_pitch - 0.02, -1.4, 1.4)

	# Posicionamento da Camera (Orbita Sintetica vs Interativa)
	var cam_pos = Vector3.ZERO
	if benchmark_suite_running:
		var theta = cfg["cam_angular_vel"] * sim_time
		cam_pos.x = cfg["cam_orbit_radius"] * cos(theta)
		cam_pos.y = cfg["cam_orbit_height"]
		cam_pos.z = cfg["cam_orbit_radius"] * sin(theta)
	else:
		cam_pos.x = interactive_cam_dist * cos(interactive_cam_pitch) * cos(interactive_cam_yaw)
		cam_pos.y = interactive_cam_dist * sin(interactive_cam_pitch)
		cam_pos.z = interactive_cam_dist * cos(interactive_cam_pitch) * sin(interactive_cam_yaw)

	camera_node.look_at_from_position(cam_pos, Vector3.ZERO, Vector3.UP)

	# Propagacao das coordenadas locais nos Spatial Nodes da Scene Tree
	for i in range(active_planet_nodes.size()):
		var p = active_planet_states[i]
		var node: MeshInstance = active_planet_nodes[i]
		var x = 0.0
		var z = 0.0
		if i > 0:
			var angle = p["initial_phase"] + p["orbital_speed"] * sim_time
			x = p["orbit_radius"] * cos(angle)
			z = p["orbit_radius"] * sin(angle)

		node.translation = Vector3(x, 0.0, z)
		node.rotation.y = p["self_rotation_speed"] * sim_time

	# Coleta de Telemetria (Equacoes 3 e 4 do artigo)
	var dt_ms = float(now_usec - last_swap_usec) / 1000.0
	last_swap_usec = now_usec
	var elapsed_stage_sec = float(now_usec - stage_start_usec) * 1e-6

	var current_stage = ALL_STAGES[current_stage_idx]

	if benchmark_suite_running:
		if in_warmup_phase:
			prev_frame_time_ms = dt_ms
			if elapsed_stage_sec >= cfg["warmup_seconds"]:
				in_warmup_phase = false
				stage_start_usec = now_usec
		else:
			if frame_counter % cfg["rss_sample_interval"] == 0:
				cached_rss_mb = _read_process_rss_mb()

			var jitter_ms = 0.0 if frame_counter == 0 else abs(dt_ms - prev_frame_time_ms)
			prev_frame_time_ms = dt_ms
			var inst_fps = (1000.0 / dt_ms) if dt_ms > 0.0001 else 0.0

			samples.append({
				"frame": frame_counter,
				"elapsed_s": elapsed_stage_sec,
				"frame_time_ms": dt_ms,
				"fps": inst_fps,
				"jitter_ms": jitter_ms,
				"rss_mb": cached_rss_mb,
				"planets": current_stage["planet_count"],
				"triangles": current_stage["total_triangles"]
			})
			frame_counter += 1

			if elapsed_stage_sec >= cfg["sample_duration_seconds"]:
				_export_stage_csv(current_stage)
				var summary = _compute_stage_summary(current_stage)
				suite_summaries.append(summary)

				print("[CONCLUIDO] ", current_stage["stage_id"],
					  " | FPS Medio: ", stepify(summary["avg_fps"], 0.01),
					  " | T_quadro: ", stepify(summary["mean_frame_ms"], 0.001), " ms",
					  " | Jitter Medio: ", stepify(summary["mean_jitter_ms"], 0.001), " ms",
					  " | RSS Pico: ", stepify(summary["peak_rss_mb"], 0.01), " MB")

				if current_stage_idx + 1 < ALL_STAGES.size():
					_activate_stage(current_stage_idx + 1)
				else:
					_export_summary_csv()
					benchmark_suite_running = false
					_activate_stage(0)
	else:
		prev_frame_time_ms = dt_ms
		if Engine.get_frames_drawn() % 30 == 0:
			cached_rss_mb = _read_process_rss_mb()

	if show_hud:
		_update_hud_text(current_stage)


func _read_process_rss_mb() -> float:
	# No Linux / Raspberry Pi 3, le diretamente /proc/self/statm conforme Secao III.D do artigo
	var f = File.new()
	if f.file_exists("/proc/self/statm"):
		if f.open("/proc/self/statm", File.READ) == OK:
			var parts = f.get_line().strip_edges().split(" ", false)
			f.close()
			if parts.size() >= 2:
				var rss_pages = float(parts[1])
				var page_size_bytes = 4096.0
				return (rss_pages * page_size_bytes) / (1024.0 * 1024.0)

	# No Windows, utiliza o monitor de memoria residente (Static + Dynamic) da engine
	var static_mem = float(OS.get_static_memory_usage())
	var dyn_mem = float(Performance.get_monitor(Performance.MEMORY_DYNAMIC))
	return (static_mem + dyn_mem) / (1024.0 * 1024.0)


func _update_hud_text(current_stage: Dictionary) -> void:
	var state_str = "MODO INTERATIVO (Mouse/WASD)"
	if benchmark_suite_running:
		state_str = "BENCHMARK (AQUECIMENTO)" if in_warmup_phase else "BENCHMARK (GRAVANDO CSV)"

	var inst_fps = (1000.0 / prev_frame_time_ms) if prev_frame_time_ms > 0.0001 else 0.0

	hud_label.text = (
		"PILHA B: Godot 3.5 (GLES2 | Spatial Nodes SceneTree)\n" +
		"Estado: %s\n" % state_str +
		"Modo: %s | Estagio: %s\n" % [current_stage["mode_name"], current_stage["stage_id"]] +
		"Planetas Ativos: %d | Triangulos Totais: %d\n" % [current_stage["planet_count"], current_stage["total_triangles"]] +
		"FPS: %.1f | T_quadro: %.3f ms | RSS: %.2f MB\n\n" % [inst_fps, prev_frame_time_ms, cached_rss_mb] +
		"[M] Trocar Modo (Densidade vs Qtd Planetas) | [1-4] Carga\n" +
		"[B] Rodar Bateria Completa (CSV) | [H] Ocultar HUD"
	)


func _get_csv_dir() -> String:
	var csv_dir = ProjectSettings.globalize_path("res://../analysis/results/csv")
	var dir = Directory.new()
	if not dir.dir_exists(csv_dir):
		dir.make_dir_recursive(csv_dir)
	return csv_dir


func _export_stage_csv(st: Dictionary) -> void:
	var filepath = _get_csv_dir() + "/godot_" + st["stage_id"] + ".csv"
	var f = File.new()
	if f.open(filepath, File.WRITE) != OK:
		print("[ERRO] Nao foi possivel salvar o CSV: ", filepath)
		return

	f.store_line("engine,mode,stage_id,frame,elapsed_s,frame_time_ms,fps,jitter_ms,rss_mb,planets,triangles")
	for s in samples:
		var line = "Godot_Pilha_B,%s,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d" % [
			st["mode_name"],
			st["stage_id"],
			s["frame"],
			s["elapsed_s"],
			s["frame_time_ms"],
			s["fps"],
			s["jitter_ms"],
			s["rss_mb"],
			s["planets"],
			s["triangles"]
		]
		f.store_line(line)
	f.close()


func _compute_stage_summary(st: Dictionary) -> Dictionary:
	var count = samples.size()
	var sum_Obj = {
		"engine": "Godot_Pilha_B",
		"mode": st["mode_name"],
		"stage_id": st["stage_id"],
		"planets": st["planet_count"],
		"triangles": st["total_triangles"],
		"frames": count,
		"avg_fps": 0.0,
		"mean_frame_ms": 0.0,
		"stddev_frame_ms": 0.0,
		"p99_frame_ms": 0.0,
		"mean_jitter_ms": 0.0,
		"max_jitter_ms": 0.0,
		"peak_rss_mb": 0.0
	}
	if count == 0:
		return sum_Obj

	var ft_list = []
	var sum_ft = 0.0
	var sum_jitter = 0.0
	var max_jitter = 0.0
	var max_rss = 0.0

	for s in samples:
		var ft = s["frame_time_ms"]
		var jt = s["jitter_ms"]
		var rs = s["rss_mb"]
		ft_list.append(ft)
		sum_ft += ft
		sum_jitter += jt
		if jt > max_jitter:
			max_jitter = jt
		if rs > max_rss:
			max_rss = rs

	var mean_ft = sum_ft / float(count)
	sum_Obj["mean_frame_ms"] = mean_ft
	sum_Obj["avg_fps"] = (1000.0 / mean_ft) if mean_ft > 0.0001 else 0.0
	sum_Obj["mean_jitter_ms"] = sum_jitter / float(count)
	sum_Obj["max_jitter_ms"] = max_jitter
	sum_Obj["peak_rss_mb"] = max_rss

	var sq_diff = 0.0
	for ft in ft_list:
		var d = ft - mean_ft
		sq_diff += d * d
	sum_Obj["stddev_frame_ms"] = sqrt(sq_diff / float(count))

	ft_list.sort()
	var p99_idx = int(floor(0.99 * float(count - 1)))
	sum_Obj["p99_frame_ms"] = ft_list[p99_idx]

	return sum_Obj


func _export_summary_csv() -> void:
	var filepath = _get_csv_dir() + "/summary_godot_pilha_b.csv"
	var f = File.new()
	if f.open(filepath, File.WRITE) != OK:
		return

	f.store_line("engine,mode,stage_id,planets,triangles,frames,avg_fps,mean_frame_ms,stddev_frame_ms,p99_frame_ms,mean_jitter_ms,max_jitter_ms,peak_rss_mb")
	for s in suite_summaries:
		var line = "%s,%s,%s,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f" % [
			s["engine"], s["mode"], s["stage_id"],
			s["planets"], s["triangles"], s["frames"],
			s["avg_fps"], s["mean_frame_ms"], s["stddev_frame_ms"],
			s["p99_frame_ms"], s["mean_jitter_ms"], s["max_jitter_ms"],
			s["peak_rss_mb"]
		]
		f.store_line(line)
	f.close()
	print("=========================================================")
	print("[SUCESSO] Bateria Godot 3.5 finalizada! CSVs salvos em: ", _get_csv_dir())
	print("=========================================================")
