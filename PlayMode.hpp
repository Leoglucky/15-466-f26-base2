#include "Mode.hpp"

#include "Scene.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <random>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	// rng for moles
	std::mt19937 rng; 

	float next_spawn_timer = 0.0f;

	// enumerating mole positions
	enum moleState {
		HIDDEN,
		RISE,
		UP,
		FALL,
	};

	// storing mole state
	struct moleTransform {
		enum moleState currentState;
		glm::vec3 hiddenPos; 
		Scene::Transform *moleTf; 
		float timer; 
		uint8_t moleIndex;
		Scene::Drawable *drawable; 
	};

	std::vector< moleTransform > moles; 

	// "success" state color storage
	GLuint texSuccess;

	// game level state
	uint16_t score = 0; 
	float time_left = 120.0f; 
	bool game_over = false; 

	/*input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;
	*/

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	/*
	//hexapod leg to wobble:
	Scene::Transform *hip = nullptr;
	Scene::Transform *upper_leg = nullptr;
	Scene::Transform *lower_leg = nullptr;
	*/

	glm::quat hip_base_rotation;
	glm::quat upper_leg_base_rotation;
	glm::quat lower_leg_base_rotation;
	float wobble = 0.0f;
	
	//camera:
	Scene::Camera *camera = nullptr;

};
