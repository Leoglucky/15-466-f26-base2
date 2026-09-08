#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "PathFont.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <numeric>

#include <random>

/* Example Hexapod Code for Reference
GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("hexapod.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path(".scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});
*/



constexpr float MIN = 4.0f; 
constexpr float MIN_LATE = 0.5f;
constexpr float MAX = 8.0f;
constexpr float MAX_LATE = 1.5f;

constexpr float POP_HEIGHT = 2.5f;
constexpr float RISE_DURATION = 0.8f;
constexpr float RISE_DURATION_LATE = 0.4f;

constexpr float UP_DURATION = 2.5f; 
constexpr float UP_DURATION_LATE = 1.5f;

constexpr float FALL_DURATION = 0.4f;

constexpr float GAME_DURATION = 120.0f;

constexpr uint8_t MAX_CONCURRENT = 6;

constexpr float SCORE_RAMP_SCALE = 60.0f;

constexpr float HIT_RADIUS_X_FRAC = 0.05f;

constexpr float HIT_RADIUS_Y_TOP_FRAC = 0.154f;
constexpr float HIT_RADIUS_Y_BOTTOM_FRAC = 0.05f;

constexpr float MISS_PENALTY = 1.0f; //seconds removed from time_left on a missed click

//computes the on-screen width (in the same units as draw_text's x/y direction vectors)
// that DrawLines::draw_text would use for this string -- mirrors its internal glyph-matching
// logic exactly, but only sums widths instead of emitting geometry, so it's safe to call
// purely for layout/centering without any visible side effects.
float text_width(std::string const &text) {
	float width = 0.0f;
	uint32_t start = 0;
	while (start < text.size()) {
		uint32_t end = start;
		uint32_t glyph = -1U;
		while (end < text.size()) {
			end += 1;
			auto f = PathFont::font.glyph_map.find(text.substr(start, end - start));
			if (f == PathFont::font.glyph_map.end()) {
				end -= 1;
				break;
			}
			glyph = f->second;
		}
		if (glyph == -1U) {
			end += 1;
			width += 0.6f; //matches the 'tofu' fallback glyph width in DrawLines::draw_text
		} else {
			width += PathFont::font.glyph_widths[glyph];
		}
		start = end;
	}
	return width;
}

GLuint mole_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > mole_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("molegame.pnct"));
	mole_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > mole_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("molegame.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = mole_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = mole_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

PlayMode::PlayMode() : scene(*mole_scene) {
	std::random_device rd;

	rng.seed(rd());

	uint8_t moleIdx = 1;

	// make a color change texture for mole hit
	glGenTextures(1, &texSuccess);

	glBindTexture(GL_TEXTURE_2D, texSuccess);
	std::vector< glm::u8vec4 > tex_data(1, glm::u8vec4(0, 255, 128, 1));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	for (auto &transform : scene.transforms) { // initialize all moles
		if (transform.name.find("Mole") != std::string::npos)
		{
			moles.push_back(
				moleTransform{
					.currentState = HIDDEN,
					.hiddenPos = transform.position,
					.moleTf = &transform,
					.timer = 0.0f,
					.moleIndex = moleIdx
				}
			);
			moleIdx++;
		}
	}

	for (auto& mole : moles) {
		for (auto& drawable : scene.drawables) {
			if (drawable.transform == mole.moleTf) {
				mole.drawable = &drawable;
				break;
			}
		}
	}

	if (moles.size() < 9)
	{
		throw std::runtime_error("Moles not initialized correctly.");
	}

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//initialize random number
	std::uniform_real_distribution<float> distribTime(MIN, MAX);
	next_spawn_timer = distribTime(rng);
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	/* Camera movement code for reference
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		} else if (evt.key.key == SDLK_A) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}
	*/
	if (game_over)
	{
		if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_R)
		{
			score = 0; 
			time_left = GAME_DURATION; 
			game_over = false;
			for (auto& mole : moles)
			{
				mole.currentState = HIDDEN;
				mole.timer = 0.0f;
				mole.moleTf->position = mole.hiddenPos; 
				mole.drawable->pipeline.textures[0].texture = lit_color_texture_program_pipeline.textures[0].texture;
			}
		}
	}

	if (!game_over && evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		glm::mat4 clip_from_world = camera->make_projection() * glm::mat4(camera->transform->make_local_from_world());

		float radiusX = HIT_RADIUS_X_FRAC * window_size.x;
		float radiusYTop = HIT_RADIUS_Y_TOP_FRAC * window_size.y;
		float radiusYBottom = HIT_RADIUS_Y_BOTTOM_FRAC * window_size.y;

		for(auto& mole : moles)
		{
			if(mole.currentState == UP)
			{
				glm::vec4 clip = clip_from_world * glm::vec4(mole.moleTf->position, 1.0f);
				glm::vec2 ndc = glm::vec2(clip) / clip.w;

				float px = (ndc.x * 0.5f + 0.5f) * (float) window_size.x;
				float py = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float) window_size.y;

				glm::vec2 clickPos(evt.button.x, evt.button.y);
				glm::vec2 molePos(px, py);

				auto compDistance = [&](glm::vec2 clickPos, glm::vec2 molePos) {
					float dx = clickPos.x - molePos.x;
					float dy = clickPos.y - molePos.y;

					//screen-space y grows downward, so dy < 0 means the click is ABOVE
					//the mole's anchor (the visible part) -- be generous there, and strict
					//below the anchor (the part still hidden in the hole):
					float radiusY = (dy < 0.0f) ? radiusYTop : radiusYBottom;

					float nx = dx / radiusX;
					float ny = dy / radiusY;

					bool hit = (nx * nx + ny * ny) <= 1.0f;

					return hit;
				};

				if (compDistance(clickPos, molePos))
				{
					score++;
					mole.currentState = FALL;
					mole.timer = 0.0f;
					mole.drawable->pipeline.textures[0].texture = texSuccess;
					return true;
				}
			}
		}

		//missed every mole -- punish the whiff:
		time_left = std::max(time_left - MISS_PENALTY, 0.0f);
		return true;
	}


	return false;
}

void PlayMode::update(float elapsed) {

	if (time_left <= 0.0f)
	{
		game_over = true;
	}

	if (game_over)
	{ // TODO:  fill in code for game over state
		return; 
	}

	//slowly rotates through [0,1):
	wobble += elapsed / 10.0f;
	wobble -= std::floor(wobble);

	// difficulty scalar
	float timeProgress = 1.0f - (time_left / GAME_DURATION);

	float scoreProgress = std::clamp((float) score / SCORE_RAMP_SCALE, 0.0f, 1.0f);

	float progress = std::max(timeProgress, scoreProgress);

	float curMin = glm::mix(MIN, MIN_LATE, progress);
	float curMax = glm::mix(MAX, MAX_LATE, progress);

	float curRise = glm::mix(RISE_DURATION, RISE_DURATION_LATE, progress);
	float curUp = glm::mix(UP_DURATION, UP_DURATION_LATE, progress);

	uint8_t maxConcurrent = 1 + (uint8_t)(progress * (MAX_CONCURRENT - 1));

	// constexpr float durationUp = 30.0f; 
	// constexpr float durationDown = 30.0f;

	/* Old Hexapod code, for reference. 
	hip->rotation = hip_base_rotation * glm::angleAxis(
		glm::radians(5.0f * std::sin(wobble * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);
	upper_leg->rotation = upper_leg_base_rotation * glm::angleAxis(
		glm::radians(7.0f * std::sin(wobble * 2.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);
	lower_leg->rotation = lower_leg_base_rotation * glm::angleAxis(
		glm::radians(10.0f * std::sin(wobble * 3.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);
	*/

	uint8_t riseUpCount = 0;

	for (auto& mole : moles)
	{
		if (mole.currentState == RISE || mole.currentState == UP)
		{
			riseUpCount++;
		}
	}

	if (next_spawn_timer <= 0.0f)
	{
		std::uniform_int_distribution<int> distribNum(1, maxConcurrent);
		uint8_t concurrTotal = (uint8_t) distribNum(rng);

		std::vector< uint8_t > candidateIndices(9);
		std::iota(candidateIndices.begin(), candidateIndices.end(), (uint8_t) 1);
		std::shuffle(candidateIndices.begin(), candidateIndices.end(), rng);
		candidateIndices.resize(concurrTotal);

		std::uniform_real_distribution<float> distribTime(curMin, curMax);
		next_spawn_timer = distribTime(rng);
		for (auto& mole : moles)
		{
			if (mole.currentState == HIDDEN && riseUpCount < maxConcurrent)
			{
				// mark as candidate to rise in rng
				bool chosen = std::find(candidateIndices.begin(), candidateIndices.end(), mole.moleIndex) != candidateIndices.end();
				if (chosen)
				{
					mole.currentState = RISE;
					mole.timer = 0.0f;
					riseUpCount++;
				}
			}
		}
	}

	for (auto& mole : moles)
	{
		glm::vec3 upPos = mole.hiddenPos + glm::vec3(0.0f, 0.0f, POP_HEIGHT);
		if (mole.currentState == RISE)
		{
			// lerp position from hiddenPos towards UP position
			float t = std::min(mole.timer / curRise, 1.0f);
			mole.moleTf->position = glm::mix(mole.hiddenPos, upPos, glm::smoothstep(0.0f, 1.0f, t));
			if (mole.timer >= curRise){
				mole.currentState = UP;
				mole.timer = 0.0f;
			}
		}
		else if (mole.currentState == UP)
		{
			// allow to be hit and hold position for timer amount until player hits mole
			if (mole.timer >= curUp)
			{
				mole.currentState = FALL; 
				mole.timer = 0.0f;
			}
		}
		else if (mole.currentState == FALL)
		{
			float t = std::min(mole.timer / FALL_DURATION, 1.0f);
			mole.moleTf->position = glm::mix(upPos, mole.hiddenPos, glm::smoothstep(0.0f, 1.0f, t));
			// lerp back down to hiddenPos, then switch back to HIDDEN and reset timer
			if (mole.timer >= FALL_DURATION)
			{
				mole.currentState = HIDDEN;
				mole.timer = 0.0f;
				mole.drawable->pipeline.textures[0].texture = lit_color_texture_program_pipeline.textures[0].texture;
			}
		}
		mole.timer += elapsed;
	}


	/*move camera: (DEPRECATED FOR NOW)
	{

		//combine inputs into a move:
		constexpr float PlayerSpeed = 30.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];

		camera->transform->position += move.x * frame_right + move.y * frame_forward;
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

	*/

	time_left -= elapsed; 
	next_spawn_timer -= elapsed; 
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		glLineWidth(2.5f);
		float ofs = 2.0f / drawable_size.y;

		if (game_over) {
			constexpr float TitleH = 0.4f;
			std::string title = "GAME OVER";
			float title_width = TitleH * text_width(title);
			glm::vec3 title_anchor(-0.5f * title_width, 0.15f, 0.0f);

			lines.draw_text(title, title_anchor,
				glm::vec3(TitleH, 0.0f, 0.0f), glm::vec3(0.0f, TitleH, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text(title, title_anchor + glm::vec3(ofs, ofs, 0.0f),
				glm::vec3(TitleH, 0.0f, 0.0f), glm::vec3(0.0f, TitleH, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));

			constexpr float SubH = 0.15f;
			std::string sub = "Final Score: " + std::to_string(score) + "   (press R to restart)";
			float sub_width = SubH * text_width(sub);
			glm::vec3 sub_anchor(-0.5f * sub_width, -0.15f, 0.0f);

			lines.draw_text(sub, sub_anchor,
				glm::vec3(SubH, 0.0f, 0.0f), glm::vec3(0.0f, SubH, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text(sub, sub_anchor + glm::vec3(ofs, ofs, 0.0f),
				glm::vec3(SubH, 0.0f, 0.0f), glm::vec3(0.0f, SubH, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		} else {
			constexpr float H = 0.15f;
			std::string hud = "Score: " + std::to_string(score) + "   Time: " + std::to_string((int)std::ceil(std::max(time_left, 0.0f)));

			lines.draw_text(hud,
				glm::vec3(-aspect + 0.1f * H, -1.0f + 0.1f * H, 0.0f),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			lines.draw_text(hud,
				glm::vec3(-aspect + 0.1f * H + ofs, -1.0f + 0.1f * H + ofs, 0.0f),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
	}
}
