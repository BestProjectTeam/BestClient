// Mumble Link C
// https://github.com/SollyBunny/mumble-link-c

#ifndef MUMBLE_H
#define MUMBLE_H

#include <wchar.h>
#include <stdbool.h>
#include <stdint.h>

#if !defined(MUMBLE_STUB) && (defined(__ANDROID__) || defined(__EMSCRIPTEN__))
	#define MUMBLE_STUB 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared memory layout for the Mumble Link positional audio plugin.
 *
 * This structure is shared between the game/application and Mumble.
 * When updating any fields, the writer **must increment @ref uiTick**
 * after all data has been written so readers can detect changes.
 *
 * @note This structure must exactly match the layout expected by Mumble.
 * @note Defined by the Mumble Link Plugin specification.
 *
 * @see https://www.mumble.info/documentation/developer/positional-audio/link-plugin/
 * @see mumble_2d_update
 * @see mumble_3d_update
 */
struct MumbleLinkedMem {
	uint32_t uiVersion; ///< Structure, should always be 2. May be 0 when the plugin unlinks
	uint32_t uiTick; ///< Tick of the game, increment after update
	float fAvatarPosition[3]; ///< Position of the mouth
	float fAvatarFront[3]; ///< Direction of the mouth
	float fAvatarTop[3]; ///< Direction of up for the mouth (perpendicular to fAvatarFront)
	wchar_t name[256]; ///< Name of the game
	float fCameraPosition[3]; ///< Position of the ears
	float fCameraFront[3]; ///< Direction of the ears
	float fCameraTop[3]; ///< Direction of up for the ears (perpendicular to fCameraFront)
	wchar_t identity[256]; ///< Name of the player in game (can be connection ID, account ID or arbritrary data, eg. JSON)
	uint32_t context_len; ///< Number of characters in context
	unsigned char context[256]; ///< Should be equal for players that should hear eachother, for example the server address, port and the player team
	wchar_t description[2048]; ///< Description of the game
};

/**
 * Holds information about the Mumble context
 *
 * @see mumble_get_linked_mem
 */
struct MumbleContext;

/**
 * Create a Mumble Link plugin context
 * This will immediately be detected by the plugin, if active
 * You can further test your program with the Positional Audio Viewer
 * In Settings->User Interface->Enable Developer Menu
 * Then it will be available in Developer->Positional Audio Viewer
 *
 * @returns A valid mumble context on success.
 * @returns NULL on failure, you may want to check `errno`.
 *
 * @see mumble_create_context_args
 */
struct MumbleContext* mumble_create_context(void);

/**
 * Same as @ref mumble_create_context but sets name and description
 *
 * @returns A valid mumble context on success.
 * @returns NULL on failure, you may want to check `errno`.
 *
 * @see mumble_create_context
 */
struct MumbleContext* mumble_create_context_args(const char* name, const char* description);

/**
 * @brief Destroy a Mumble context.
 *
 * Frees all resources associated with the given Mumble context.
 * Using the context after this function returns results in <b>undefined behavior</b>.
 *
 * @note Invalidates previous calls to @ref mumble_get_linked_mem
 *
 * @param context Pointer to the Mumble context to destroy. Can be NULL.
 */
void mumble_destroy_context(struct MumbleContext** context);

/**
 * @brief Retrieve the shared Mumble Link memory for a context.
 *
 * Returns a pointer to the shared memory block used for positional
 * audio communication with Mumble.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 *
 * @return Pointer to the associated @ref MumbleLinkedMem structure. This function never returns NULL.
 *
 * @see MumbleLinkedMem
 */
struct MumbleLinkedMem* mumble_get_linked_mem(struct MumbleContext* context);

// Utils

/**
 * @brief Check if a Mumble Link context has been unlinked.
 *
 * Mumble may automatically unlink a context after some time of inactivity.
 * You may do this if the player is disconnected in game.
 * You should destroy and recreate the context if this function returns `true`
 *
 * @note This detects unlink by checking uiVersion.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 *
 * @return `true` if a relink is needed, `false` otherwise.
 *
 * @see mumble_relink
 */
bool mumble_relink_needed(struct MumbleContext* context);

/**
 * @brief Set the name of the game.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param name Null-terminated string representing the game name.
 *
 * @return `true` if the name was successfully updated, `false` otherwise.
 */
bool mumble_set_name(struct MumbleContext* context, const char* name);

/**
 * @brief Set the identity of the player.
 *
 * Updates the `identity` field in the @ref MumbleLinkedMem.
 * This can be a connection ID, account ID, or any arbitrary data (for example, JSON).
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param identity Null-terminated string representing the player's identity.
 *
 * @return `true` if the identity was successfully updated, `false` otherwise.
 */
bool mumble_set_identity(struct MumbleContext* context, const char* identity);

/**
 * @brief Set the context string used to group players.
 *
 * Updates the `context` field in the @ref MumbleLinkedMem. Players with
 * the same context string can hear each other. Typical usage includes
 * server address, port, or team.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param mumbleContext Null-terminated string representing the context.
 *
 * @return `true` if the context was successfully updated, `false` otherwise.
 */
bool mumble_set_context(struct MumbleContext* context, const char* mumbleContext);

/**
 * @brief Set the description of the game.
 *
 * Updates the `description` field in the @ref MumbleLinkedMem.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param description Null-terminated string describing the game.
 *
 * @return `true` if the description was successfully updated, `false` otherwise.
 */
bool mumble_set_description(struct MumbleContext* context, const char* description);

// Simple interface

/**
 * Update the 2D position of the player in the Mumble context.
 *
 * @note If you update the data in anyway but this function, you should use `mumble_3d_update(0.0f, 0.0f, 0.0f)` beforehand.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param x X coordinate of the player.
 * @param y Y coordinate of the player.
 */
void mumble_2d_update(struct MumbleContext* context, float x, float y);

/**
 * Update the 3D position of the player in the Mumble context.
 *
 * @param context Pointer to a valid Mumble context. Must not be NULL.
 * @param x X coordinate of the player.
 * @param y Y coordinate of the player.
 * @param z Z coordinate of the player.
 */
void mumble_3d_update(struct MumbleContext* context, float x, float y, float z);

#ifdef __cplusplus
}
#endif

#endif
