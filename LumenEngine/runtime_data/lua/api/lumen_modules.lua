---@meta
-- Native Lumen libraries (generated from the engine; parameters are documented in docs/libraries.md).

lumen.mathx = {}
---@param ... any
function lumen.mathx.angle_diff(...) end
---@param ... any
function lumen.mathx.approach(...) end
---@param ... any
function lumen.mathx.approx(...) end
---@param ... any
function lumen.mathx.bezier3(...) end
---@param ... any
function lumen.mathx.binomial(...) end
---@param ... any
function lumen.mathx.catmull_rom(...) end
---@param ... any
---@return number
function lumen.mathx.clamp(...) end
---@param ... any
function lumen.mathx.cubic_bezier(...) end
---@param ... any
function lumen.mathx.damp(...) end
---@param ... any
---@return number
function lumen.mathx.deg(...) end
---@param ... any
function lumen.mathx.ease(...) end
---@param ... any
function lumen.mathx.ease_names(...) end
---@type number
lumen.mathx.epsilon = 1e-06
---@param ... any
function lumen.mathx.fract(...) end
---@param ... any
function lumen.mathx.gcd(...) end
---@param ... any
function lumen.mathx.inverse_lerp(...) end
---@param ... any
function lumen.mathx.is_pow2(...) end
---@param ... any
---@return number
function lumen.mathx.lerp(...) end
---@param ... any
function lumen.mathx.lerp_angle(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4(...) end
---@param ... any
function lumen.mathx.mat4_from_table(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_look_at(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_ortho(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_perspective(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_rotation(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_scale(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_translation(...) end
---@param ... any
---@return Mat4
function lumen.mathx.mat4_trs(...) end
---@param ... any
function lumen.mathx.next_pow2(...) end
---@type number
lumen.mathx.pi = 3.1415926535898
---@param ... any
function lumen.mathx.pingpong(...) end
---@param ... any
---@return Quat
function lumen.mathx.quat(...) end
---@param ... any
---@return Quat
function lumen.mathx.quat_axis_angle(...) end
---@param ... any
---@return Quat
function lumen.mathx.quat_euler(...) end
---@param ... any
function lumen.mathx.quat_from_mat4(...) end
---@param ... any
---@return Quat
function lumen.mathx.quat_from_to(...) end
---@param ... any
---@return Quat
function lumen.mathx.quat_look(...) end
---@param ... any
---@return number
function lumen.mathx.rad(...) end
---@param ... any
---@return number
function lumen.mathx.remap(...) end
---@param ... any
---@return number
function lumen.mathx.round(...) end
---@param ... any
function lumen.mathx.saturate(...) end
---@param ... any
---@return number
function lumen.mathx.sign(...) end
---@param ... any
function lumen.mathx.smooth_damp(...) end
---@param ... any
function lumen.mathx.smootherstep(...) end
---@param ... any
---@return number
function lumen.mathx.smoothstep(...) end
---@param ... any
function lumen.mathx.spring(...) end
---@type number
lumen.mathx.tau = 6.2831853071796
---@param ... any
---@return Vec2
function lumen.mathx.vec2(...) end
---@param ... any
---@return Vec3
function lumen.mathx.vec3(...) end
---@param ... any
---@return Vec4
function lumen.mathx.vec4(...) end
---@param ... any
function lumen.mathx.wrap(...) end

lumen.random = {}
---@param ... any
---@return boolean
function lumen.random.bool(...) end
---@param ... any
function lumen.random.bytes(...) end
---@param ... any
function lumen.random.dice(...) end
---@param ... any
---@return number
function lumen.random.float(...) end
---@param ... any
function lumen.random.hash(...) end
---@param ... any
function lumen.random.in_circle(...) end
---@param ... any
function lumen.random.in_sphere(...) end
---@param ... any
---@return integer
function lumen.random.int(...) end
---@param ... any
function lumen.random.new(...) end
---@param ... any
function lumen.random.normal(...) end
---@param ... any
function lumen.random.pick(...) end
---@param ... any
---@return number
function lumen.random.range(...) end
---@param ... any
function lumen.random.sample(...) end
---@param ... any
function lumen.random.seed(...) end
---@param ... any
function lumen.random.shuffle(...) end
---@param ... any
function lumen.random.unit2(...) end
---@param ... any
function lumen.random.unit3(...) end
---@param ... any
function lumen.random.weighted(...) end

lumen.noise = {}
---@param ... any
function lumen.noise.curl2(...) end
---@param ... any
---@return number
function lumen.noise.fbm(...) end
---@param ... any
function lumen.noise.grid(...) end
---@param ... any
---@return number
function lumen.noise.perlin2(...) end
---@param ... any
---@return number
function lumen.noise.perlin3(...) end
---@param ... any
---@return number
function lumen.noise.simplex2(...) end
---@param ... any
---@return number
function lumen.noise.simplex3(...) end
---@param ... any
function lumen.noise.value2(...) end
---@param ... any
function lumen.noise.value3(...) end
---@param ... any
function lumen.noise.worley2(...) end
---@param ... any
function lumen.noise.worley3(...) end

lumen.geom = {}
---@param ... any
function lumen.geom.aabb_contains(...) end
---@param ... any
function lumen.geom.aabb_overlap(...) end
---@param ... any
function lumen.geom.barycentric(...) end
---@param ... any
function lumen.geom.bezier(...) end
---@param ... any
function lumen.geom.bounds(...) end
---@param ... any
function lumen.geom.catmull_rom_spline(...) end
---@param ... any
function lumen.geom.circle_overlap(...) end
---@param ... any
function lumen.geom.closest_on_segment(...) end
---@param ... any
function lumen.geom.convex_hull(...) end
---@param ... any
function lumen.geom.line_circle(...) end
---@param ... any
function lumen.geom.point_in_polygon(...) end
---@param ... any
function lumen.geom.polygon_area(...) end
---@param ... any
function lumen.geom.polygon_centroid(...) end
---@param ... any
function lumen.geom.ray_aabb(...) end
---@param ... any
function lumen.geom.ray_plane(...) end
---@param ... any
function lumen.geom.ray_sphere(...) end
---@param ... any
function lumen.geom.ray_triangle(...) end
---@param ... any
function lumen.geom.rect_overlap(...) end
---@param ... any
function lumen.geom.segment_intersect2(...) end
---@param ... any
function lumen.geom.simplify(...) end
---@param ... any
function lumen.geom.sphere_aabb(...) end
---@param ... any
function lumen.geom.sphere_overlap(...) end
---@param ... any
function lumen.geom.triangle_normal(...) end
---@param ... any
function lumen.geom.triangulate(...) end

lumen.color = {}
---@param ... any
function lumen.color.contrast(...) end
---@param ... any
function lumen.color.from_hsl(...) end
---@param ... any
function lumen.color.from_hsv(...) end
---@param ... any
function lumen.color.from_oklab(...) end
---@param ... any
function lumen.color.gradient(...) end
---@param ... any
function lumen.color.kelvin(...) end
---@param ... any
function lumen.color.lighten(...) end
---@param ... any
function lumen.color.luminance(...) end
---@param ... any
function lumen.color.mix(...) end
---@param ... any
function lumen.color.palette(...) end
---@param ... any
function lumen.color.parse(...) end
---@param ... any
function lumen.color.rgb(...) end
---@param ... any
function lumen.color.rotate_hue(...) end
---@param ... any
function lumen.color.saturate(...) end
---@param ... any
function lumen.color.text_on(...) end
---@param ... any
function lumen.color.to_hex(...) end
---@param ... any
function lumen.color.to_hsl(...) end
---@param ... any
function lumen.color.to_hsv(...) end
---@param ... any
function lumen.color.to_linear(...) end
---@param ... any
function lumen.color.to_oklab(...) end
---@param ... any
function lumen.color.to_srgb(...) end
---@param ... any
function lumen.color.with_alpha(...) end

lumen.json = {}
---@param ... any
function lumen.json.array(...) end
---@param ... any
---@return any?
function lumen.json.decode(...) end
---@param ... any
---@return string?
function lumen.json.encode(...) end
---@param ... any
function lumen.json.load(...) end
---@param ... any
function lumen.json.object(...) end
---@param ... any
function lumen.json.save(...) end

lumen.hash = {}
---@param ... any
function lumen.hash.adler32(...) end
---@param ... any
function lumen.hash.base64_decode(...) end
---@param ... any
function lumen.hash.base64_encode(...) end
---@param ... any
function lumen.hash.crc32(...) end
---@param ... any
function lumen.hash.equal(...) end
---@param ... any
function lumen.hash.fnv1a64(...) end
---@param ... any
function lumen.hash.hex_decode(...) end
---@param ... any
function lumen.hash.hex_encode(...) end
---@param ... any
function lumen.hash.hmac(...) end
---@param ... any
function lumen.hash.md5(...) end
---@param ... any
function lumen.hash.pbkdf2(...) end
---@param ... any
function lumen.hash.random_bytes(...) end
---@param ... any
function lumen.hash.sha1(...) end
---@param ... any
function lumen.hash.sha256(...) end
---@param ... any
function lumen.hash.sha256_file(...) end
---@param ... any
function lumen.hash.uuid4(...) end

lumen.regex = {}
---@param ... any
function lumen.regex.compile(...) end
---@param ... any
function lumen.regex.escape(...) end
---@param ... any
function lumen.regex.find(...) end
---@param ... any
function lumen.regex.find_all(...) end
---@param ... any
function lumen.regex.gmatch(...) end
---@param ... any
function lumen.regex.gsub(...) end
---@param ... any
function lumen.regex.match(...) end
---@param ... any
function lumen.regex.split(...) end
---@param ... any
function lumen.regex.test(...) end

lumen.utf8x = {}
---@param ... any
function lumen.utf8x.capitalize(...) end
---@param ... any
function lumen.utf8x.chars(...) end
---@param ... any
function lumen.utf8x.codepoints(...) end
---@param ... any
function lumen.utf8x.from_codepoints(...) end
---@param ... any
function lumen.utf8x.is_valid(...) end
---@param ... any
function lumen.utf8x.len(...) end
---@param ... any
function lumen.utf8x.lower(...) end
---@param ... any
function lumen.utf8x.pad(...) end
---@param ... any
function lumen.utf8x.reverse(...) end
---@param ... any
function lumen.utf8x.sub(...) end
---@param ... any
function lumen.utf8x.truncate(...) end
---@param ... any
function lumen.utf8x.upper(...) end
---@param ... any
function lumen.utf8x.width(...) end

lumen.fs = {}
---@param ... any
function lumen.fs.absolute(...) end
---@param ... any
function lumen.fs.append(...) end
---@param ... any
function lumen.fs.copy(...) end
---@param ... any
function lumen.fs.cwd(...) end
---@param ... any
function lumen.fs.exists(...) end
---@param ... any
function lumen.fs.ext(...) end
---@param ... any
function lumen.fs.find_up(...) end
---@param ... any
function lumen.fs.glob(...) end
---@param ... any
function lumen.fs.is_dir(...) end
---@param ... any
function lumen.fs.is_file(...) end
---@param ... any
function lumen.fs.join(...) end
---@param ... any
function lumen.fs.list(...) end
---@param ... any
function lumen.fs.make_temp_dir(...) end
---@param ... any
function lumen.fs.mkdir(...) end
---@param ... any
function lumen.fs.mtime(...) end
---@param ... any
function lumen.fs.name(...) end
---@param ... any
function lumen.fs.normalize(...) end
---@param ... any
function lumen.fs.parent(...) end
---@param ... any
function lumen.fs.read(...) end
---@param ... any
function lumen.fs.read_lines(...) end
---@param ... any
function lumen.fs.relative(...) end
---@param ... any
function lumen.fs.remove(...) end
---@param ... any
function lumen.fs.remove_all(...) end
---@param ... any
function lumen.fs.rename(...) end
---@param ... any
function lumen.fs.sanitize(...) end
---@param ... any
function lumen.fs.size(...) end
---@param ... any
function lumen.fs.stem(...) end
---@param ... any
function lumen.fs.temp_dir(...) end
---@param ... any
function lumen.fs.watch(...) end
---@param ... any
function lumen.fs.write(...) end

lumen.http = {}
---@param ... any
function lumen.http.download(...) end
---@param ... any
function lumen.http.download_async(...) end
---@param ... any
function lumen.http.get(...) end
---@param ... any
function lumen.http.get_async(...) end
---@param ... any
function lumen.http.parse_url(...) end
---@param ... any
function lumen.http.url_decode(...) end
---@param ... any
function lumen.http.url_encode(...) end

lumen.time = {}
---@param ... any
function lumen.time.date(...) end
---@param ... any
function lumen.time.duration(...) end
---@param ... any
function lumen.time.format(...) end
---@param ... any
function lumen.time.iso(...) end
---@param ... any
function lumen.time.make(...) end
---@param ... any
function lumen.time.monotonic(...) end
---@param ... any
function lumen.time.now(...) end
---@param ... any
function lumen.time.parse_iso(...) end
---@param ... any
function lumen.time.sleep(...) end
---@param ... any
function lumen.time.stopwatch(...) end
---@param ... any
function lumen.time.utc_offset(...) end

lumen.net = {}
---@param ... any
function lumen.net.hostname(...) end
---@param ... any
function lumen.net.is_ip(...) end
---@param ... any
function lumen.net.resolve(...) end
---@param ... any
function lumen.net.select(...) end
---@param ... any
function lumen.net.tcp_connect(...) end
---@param ... any
function lumen.net.tcp_listen(...) end
---@param ... any
function lumen.net.udp_open(...) end

lumen.system = {}
---@param ... any
function lumen.system.arch(...) end
---@param ... any
function lumen.system.cpu_count(...) end
---@param ... any
function lumen.system.environment(...) end
---@param ... any
function lumen.system.exe_dir(...) end
---@param ... any
function lumen.system.getenv(...) end
---@param ... any
function lumen.system.home_dir(...) end
---@param ... any
function lumen.system.host(...) end
---@param ... any
function lumen.system.memory(...) end
---@param ... any
function lumen.system.open(...) end
---@param ... any
function lumen.system.os(...) end
---@param ... any
function lumen.system.pid(...) end
---@param ... any
function lumen.system.reveal(...) end
---@param ... any
function lumen.system.setenv(...) end
---@param ... any
function lumen.system.user_data_dir(...) end

lumen.zip = {}
---@param ... any
function lumen.zip.create(...) end
---@param ... any
function lumen.zip.open(...) end
---@param ... any
function lumen.zip.open_memory(...) end

lumen.compress = {}
---@param ... any
function lumen.compress.deflate(...) end
---@param ... any
function lumen.compress.gunzip(...) end
---@param ... any
function lumen.compress.gzip(...) end
---@param ... any
function lumen.compress.inflate(...) end
---@param ... any
function lumen.compress.zlib_compress(...) end
---@param ... any
function lumen.compress.zlib_decompress(...) end

lumen.process = {}
---@param ... any
function lumen.process.quote(...) end
---@param ... any
function lumen.process.run(...) end
---@param ... any
function lumen.process.spawn(...) end
---@param ... any
function lumen.process.which(...) end

lumen.log = {}
---@param ... any
function lumen.log.channel(...) end
---@param ... any
function lumen.log.debug(...) end
---@param ... any
function lumen.log.error(...) end
---@param ... any
function lumen.log.info(...) end
---@param ... any
function lumen.log.set_level(...) end
---@param ... any
function lumen.log.warn(...) end

---@class Vec2
local Vec2 = {}
---@param ... any
---@return number
function Vec2:length(...) end
---@param ... any
function Vec2:length_sq(...) end
---@param ... any
---@return Vec2
function Vec2:normalize(...) end
---@param ... any
---@return number
function Vec2:dot(...) end
---@param ... any
function Vec2:cross(...) end
---@param ... any
---@return number
function Vec2:distance(...) end
---@param ... any
function Vec2:distance_sq(...) end
---@param ... any
---@return Vec2
function Vec2:lerp(...) end
---@param ... any
function Vec2:clamp_length(...) end
---@param ... any
function Vec2:reflect(...) end
---@param ... any
function Vec2:project(...) end
---@param ... any
function Vec2:angle(...) end
---@param ... any
function Vec2:rotate(...) end
---@param ... any
function Vec2:perp(...) end
---@param ... any
function Vec2:heading(...) end
---@param ... any
function Vec2:unpack(...) end
---@param ... any
---@return Vec2
function Vec2:copy(...) end
---@param ... any
function Vec2:set(...) end
---@param ... any
function Vec2:to_table(...) end
---@param ... any
function Vec2:approx(...) end
---@param ... any
function Vec2:move_towards(...) end
---@param ... any
function Vec2:slerp(...) end

---@class Vec3
local Vec3 = {}
---@param ... any
---@return number
function Vec3:length(...) end
---@param ... any
---@return number
function Vec3:length_sq(...) end
---@param ... any
---@return Vec3
function Vec3:normalize(...) end
---@param ... any
---@return number
function Vec3:dot(...) end
---@param ... any
---@return Vec3
function Vec3:cross(...) end
---@param ... any
---@return number
function Vec3:distance(...) end
---@param ... any
---@return number
function Vec3:distance_sq(...) end
---@param ... any
---@return Vec3
function Vec3:lerp(...) end
---@param ... any
---@return Vec3
function Vec3:clamp_length(...) end
---@param ... any
---@return Vec3
function Vec3:reflect(...) end
---@param ... any
---@return Vec3
function Vec3:project(...) end
---@param ... any
---@return number
function Vec3:angle(...) end
---@param ... any
function Vec3:rotate(...) end
---@param ... any
function Vec3:perp(...) end
---@param ... any
function Vec3:heading(...) end
---@param ... any
function Vec3:unpack(...) end
---@param ... any
---@return Vec3
function Vec3:copy(...) end
---@param ... any
function Vec3:set(...) end
---@param ... any
---@return number[]
function Vec3:to_table(...) end
---@param ... any
---@return boolean
function Vec3:approx(...) end
---@param ... any
---@return Vec3
function Vec3:move_towards(...) end
---@param ... any
function Vec3:slerp(...) end

---@class Vec4
local Vec4 = {}
---@param ... any
---@return number
function Vec4:length(...) end
---@param ... any
function Vec4:length_sq(...) end
---@param ... any
---@return Vec4
function Vec4:normalize(...) end
---@param ... any
---@return number
function Vec4:dot(...) end
---@param ... any
function Vec4:cross(...) end
---@param ... any
function Vec4:distance(...) end
---@param ... any
function Vec4:distance_sq(...) end
---@param ... any
---@return Vec4
function Vec4:lerp(...) end
---@param ... any
function Vec4:clamp_length(...) end
---@param ... any
function Vec4:reflect(...) end
---@param ... any
function Vec4:project(...) end
---@param ... any
function Vec4:angle(...) end
---@param ... any
function Vec4:rotate(...) end
---@param ... any
function Vec4:perp(...) end
---@param ... any
function Vec4:heading(...) end
---@param ... any
function Vec4:unpack(...) end
---@param ... any
---@return Vec4
function Vec4:copy(...) end
---@param ... any
function Vec4:set(...) end
---@param ... any
function Vec4:to_table(...) end
---@param ... any
function Vec4:approx(...) end
---@param ... any
function Vec4:move_towards(...) end
---@param ... any
function Vec4:slerp(...) end

---@class Quat
local Quat = {}
---@param ... any
function Quat:normalize(...) end
---@param ... any
function Quat:dot(...) end
---@param ... any
function Quat:rotate(...) end
---@param ... any
function Quat:unpack(...) end
---@param ... any
function Quat:slerp(...) end
---@param ... any
function Quat:nlerp(...) end
---@param ... any
function Quat:inverse(...) end
---@param ... any
function Quat:to_euler(...) end
---@param ... any
function Quat:to_axis_angle(...) end
---@param ... any
function Quat:angle_to(...) end
---@param ... any
function Quat:to_mat4(...) end

---@class Mat4
local Mat4 = {}
---@param ... any
function Mat4:copy(...) end
---@param ... any
function Mat4:set(...) end
---@param ... any
function Mat4:to_table(...) end
---@param ... any
function Mat4:inverse(...) end
---@param ... any
function Mat4:transpose(...) end
---@param ... any
function Mat4:determinant(...) end
---@param ... any
function Mat4:transform_point(...) end
---@param ... any
function Mat4:transform_dir(...) end
---@param ... any
function Mat4:decompose(...) end
---@param ... any
function Mat4:get(...) end

