#include <emscripten.h>
#include <cstdio>

EM_JS(void, mostrar_estado_web, (), {
    const estado = document.getElementById("estado-wasm");

    if (estado) {
        estado.textContent = "WebAssembly ejecutado correctamente.";
        estado.classList.add("correcto");
    }
});

int main() {
    std::printf("Black Web: prueba inicial iniciada.\\n");
    mostrar_estado_web();
    return 0;
}
