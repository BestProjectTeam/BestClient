#define _CRT_SECURE_NO_WARNINGS // MSVC

#include "mumble.h"

#if MUMBLE_STUB == 1

struct MumbleContext {
	struct MumbleLinkedMem* lm;
};

struct MumbleContext* mumble_create_context(void) { return NULL; }
struct MumbleContext* mumble_create_context_args(const char* name, const char* description) { (void)name; (void)description; return NULL; }
void mumble_destroy_context(struct MumbleContext** context) { (void)context; }
struct MumbleLinkedMem* mumble_get_linked_mem(struct MumbleContext* context) { return context->lm; }
// Utils
bool mumble_relink_needed(struct MumbleContext* context) { (void)context; return false; }
bool mumble_set_name(struct MumbleContext* context, const char* name) { (void)context; (void)name; return false; }
bool mumble_set_identity(struct MumbleContext* context, const char* identity) { (void)context; (void)identity; return false; }
bool mumble_set_context(struct MumbleContext* context, const char* mumbleContext) { (void)context; (void)mumbleContext; return false; }
bool mumble_set_description(struct MumbleContext* context, const char* description) { (void)context; (void)description; return false; }
// Simple interface
void mumble_2d_update(struct MumbleContext* context, float x, float y) { (void)context; (void)x; (void)y; }
void mumble_3d_update(struct MumbleContext* context, float x, float y, float z) { (void)context; (void)x; (void)y; (void)z; }

#else

#include <stdlib.h>

#if defined(_WIN32)
	#include <windows.h>
	#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
		static_assert(sizeof(uint32_t) == sizeof(UINT32), "uint32 and UINT32 must be the same size");
		static_assert(sizeof(uint32_t) == sizeof(DWORD), "uint32 and DWORD must be the same size");
	#else
		_Static_assert(sizeof(uint32_t) == sizeof(UINT32), "uint32 and UINT32 must be the same size");
		_Static_assert(sizeof(uint32_t) == sizeof(DWORD), "uint32 and DWORD must be the same size");
	#endif
#else
	#include <stdio.h>
	#include <string.h>
	#include <unistd.h>
	#include <sys/mman.h>
	#include <fcntl.h>
#endif

struct MumbleContext {
	struct MumbleLinkedMem* lm;
	#if defined(_WIN32)
		HANDLE hMapObject;
	#else
		int shmfd;
		char memname[128];
	#endif
};

struct MumbleContext* mumble_create_context(void) {
	struct MumbleLinkedMem* lm = NULL;

	#if defined(_WIN32)
		HANDLE hMapObject = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MumbleLink");
		if (hMapObject == NULL)
			return NULL;

		lm = (struct MumbleLinkedMem*)MapViewOfFile(hMapObject, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct MumbleLinkedMem));
		if (lm == NULL) {
			CloseHandle(hMapObject);
			return NULL;
		}
	#else
		char memname[128];
		snprintf(memname, sizeof(memname), "/MumbleLink.%d", getuid());

		int shmfd = shm_open(memname, O_RDWR, S_IRUSR | S_IWUSR);

		if (shmfd < 0)
			return NULL;

		lm = (struct MumbleLinkedMem*)mmap(NULL, sizeof(struct MumbleLinkedMem), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);

		if (lm == (void*)(-1) || lm == NULL) {
			close(shmfd);
			return NULL;
		}
	#endif

	struct MumbleContext* out = (struct MumbleContext*)malloc(sizeof(struct MumbleContext));
	if (!out) {
		#if defined(_WIN32)
			CloseHandle(hMapObject);
		#else
			munmap(lm, sizeof(struct MumbleLinkedMem));
			close(shmfd);
		#endif
		return NULL;
	}

	out->lm = lm;
	#if defined(_WIN32)
		out->hMapObject = hMapObject;
	#else
		strcpy(out->memname, memname);
		out->shmfd = shmfd;
	#endif

	out->lm->uiVersion = 2;
	out->lm->uiTick = 0;
	memset(out->lm->fAvatarFront, 0, sizeof(out->lm->fAvatarFront));
	memset(out->lm->fAvatarPosition, 0, sizeof(out->lm->fAvatarFront));
	memset(out->lm->fAvatarTop, 0, sizeof(out->lm->fAvatarFront));
	out->lm->fAvatarTop[1] = 1.0f;
	out->lm->name[0] = L'\0';
	memset(out->lm->fCameraPosition, 0, sizeof(out->lm->fAvatarFront));
	memset(out->lm->fCameraFront, 0, sizeof(out->lm->fAvatarFront));
	memset(out->lm->fCameraTop, 0, sizeof(out->lm->fAvatarFront));
	out->lm->fCameraTop[1] = 1.0f;
	out->lm->identity[0] = L'\0';
	out->lm->context_len = 0;
	out->lm->context[0] = '\0';
	out->lm->description[0] = L'\0';
	return out;
}

struct MumbleContext* mumble_create_context_args(const char* name, const char* description) {
	struct MumbleContext* context = mumble_create_context();
	if (context == NULL)
		return NULL;
	if (!mumble_set_name(context, name)) {
		mumble_destroy_context(&context);
		return NULL;
	}
	if (!mumble_set_description(context, description)) {
		mumble_destroy_context(&context);
		return NULL;
	}
	return context;
}

void mumble_destroy_context(struct MumbleContext** context) {
	if (context == NULL) {
		return;
	}
	if (*context == NULL) {
		return;
	}
	#if defined(_WIN32)
		CloseHandle((*context)->hMapObject);
	#else
		munmap((*context)->lm, sizeof(struct MumbleLinkedMem));
		close((*context)->shmfd);
	#endif
	free((*context));
	*context = NULL;
}

struct MumbleLinkedMem* mumble_get_linked_mem(struct MumbleContext* context) {
	return context->lm;
}

// Utils

bool mumble_relink_needed(struct MumbleContext* context) {
	return context->lm->uiVersion == 0;
}

bool mumble_set_name(struct MumbleContext* context, const char* name) {
	size_t len = strlen(name);
	if (len + 1 > sizeof(context->lm->name) / sizeof(*context->lm->name))
		return false;
	if (mbstowcs(context->lm->name, name, len + 1) == (size_t) - 1)
		return false;
	return true;
}

bool mumble_set_identity(struct MumbleContext* context, const char* identity) {
	size_t len = strlen(identity);
	if (len + 1 > sizeof(context->lm->identity) / sizeof(*context->lm->identity))
		return false;
	if (mbstowcs(context->lm->identity, identity, len + 1) == (size_t) - 1)
		return false;
	return true;
}

bool mumble_set_context(struct MumbleContext* context, const char* mumbleContext) {
	size_t len = strlen(mumbleContext);
	if (len + 1 > sizeof(context->lm->context))
		return false;
	strcpy((char*)context->lm->context, mumbleContext);
	#ifdef _WIN32
		context->lm->context_len = (UINT32)len;
	#else
		context->lm->context_len = (uint32_t)len;
	#endif
	return true;
}

bool mumble_set_description(struct MumbleContext* context, const char* description) {
	size_t len = strlen(description);
	if (len + 1 > sizeof(context->lm->description) / sizeof(*context->lm->description))
		return false;
	if (mbstowcs(context->lm->description, description, len + 1) == (size_t) - 1)
		return false;
	return true;
}

// Simple interface

void mumble_2d_update(struct MumbleContext* context, float x, float y) {
	context->lm->fAvatarPosition[0] = context->lm->fCameraPosition[0] = x;
	context->lm->fAvatarPosition[2] = context->lm->fCameraPosition[2] = y;
	context->lm->uiTick += 1;
}

void mumble_3d_update(struct MumbleContext* context, float x, float y, float z) {
	context->lm->fAvatarPosition[0] = context->lm->fCameraPosition[0] = x;
	context->lm->fAvatarPosition[1] = context->lm->fCameraPosition[1] = y;
	context->lm->fAvatarPosition[2] = context->lm->fCameraPosition[2] = z;
	context->lm->uiTick += 1;
}

#endif
