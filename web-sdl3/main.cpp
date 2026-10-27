
#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>

struct EstadoPrueba {
    SDL_Window* ventana = nullptr;
    SDL_Renderer* renderizador = nullptr;
    int interacciones = 0;
};

SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
    auto* estado = new EstadoPrueba();
    *appstate = estado;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("Error al iniciar SDL3: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
            "BLACK - Prueba SDL3",
            960,
            540,
            SDL_WINDOW_RESIZABLE,
            &estado->ventana,
            &estado->renderizador)) {
        std::printf("Error al crear la ventana: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderDrawColor(
        estado->renderizador, 24, 32, 46, 255
    );

    std::printf(
        "SDL3 se inicio correctamente. "
        "Haz clic o toca la pantalla.\n"
    );

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(
    void* appstate,
    SDL_Event* evento
) {
    auto* estado = static_cast<EstadoPrueba*>(appstate);

    if (evento->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (evento->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        evento->type == SDL_EVENT_FINGER_DOWN) {
        estado->interacciones++;

        std::printf(
            "Interaccion detectada: %d\n",
            estado->interacciones
        );
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* estado = static_cast<EstadoPrueba*>(appstate);

    SDL_SetRenderDrawColor(
        estado->renderizador, 24, 32, 46, 255
    );
    SDL_RenderClear(estado->renderizador);

    SDL_FRect rectangulo = {
        80.0f, 80.0f, 800.0f, 380.0f
    };

    SDL_SetRenderDrawColor(
        estado->renderizador, 45, 75, 110, 255
    );
    SDL_RenderFillRect(estado->renderizador, &rectangulo);

    SDL_SetRenderDrawColor(
        estado->renderizador, 220, 225, 235, 255
    );
    SDL_RenderRect(estado->renderizador, &rectangulo);

    SDL_RenderPresent(estado->renderizador);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* estado = static_cast<EstadoPrueba*>(appstate);

    if (estado != nullptr) {
        if (estado->renderizador != nullptr) {
            SDL_DestroyRenderer(estado->renderizador);
        }

        if (estado->ventana != nullptr) {
            SDL_DestroyWindow(estado->ventana);
        }

        delete estado;
    }

    SDL_Quit();
}
