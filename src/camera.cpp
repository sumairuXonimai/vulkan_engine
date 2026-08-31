#include "camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

void Camera::update()
{
	glm::mat4 cameraRotation = getRotationMatrix();
	position += glm::vec3(cameraRotation * glm::vec4(velocity * 0.5f, 0.f));
}

void Camera::processSDLEvent(SDL_Event& e)
{
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_w) { velocity.z = -1; }
        if (e.key.keysym.sym == SDLK_s) { velocity.z = 1; }
        if (e.key.keysym.sym == SDLK_a) { velocity.x = -1; }
        if (e.key.keysym.sym == SDLK_d) { velocity.x = 1; }
    }

    const Uint8* state = SDL_GetKeyboardState(NULL);

    if (e.type == SDL_KEYUP) {
        if (e.key.keysym.sym == SDLK_w) { velocity.z = state[SDL_SCANCODE_S]; }
        if (e.key.keysym.sym == SDLK_s) { velocity.z = -state[SDL_SCANCODE_W]; }
        if (e.key.keysym.sym == SDLK_a) { velocity.x = state[SDL_SCANCODE_D]; }
        if (e.key.keysym.sym == SDLK_d) { velocity.x = -state[SDL_SCANCODE_A]; }
    }

    if (e.type == SDL_MOUSEMOTION) {
        yaw += (float)e.motion.xrel / 200.f;
        pitch -= (float)e.motion.yrel / 200.f;
    }
}

glm::mat4 Camera::getViewMatrix()
{
    glm::mat4 cameraTranslation{ glm::translate(glm::mat4{ 1.f }, position) };
    glm::mat4 cameraRotation{ getRotationMatrix() };
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix()
{
    glm::quat pitchRotation{ glm::angleAxis(pitch, glm::vec3(1.f, 0, 0)) };
    glm::quat yawRotation{ glm::angleAxis(yaw, glm::vec3(0, -1.f, 0)) };
    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}