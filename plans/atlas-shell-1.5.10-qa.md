# Atlas Shell 1.5.10 — gate de QA físico

Baseline recuperable: Atlas Ink 1.5.9 en el otro slot OTA.

## Antes de instalar

- Confirmar firma, SHA-256, tamaño, tipo ESP32-C3 y metadata `1.5.10` del binario final.
- Confirmar que la OTA automática sigue desactivada y que solo se usa actualización manual.
- Registrar slot activo, slot destino y selector OTA.
- No borrar NVS, SD, libros, configuración Atlas, bootloader ni tabla de particiones.

## 0–30 segundos

- Arranque visible `Atlas Ink` y versión `1.5.10`.
- Home principal Atlas, no portada ebook-first.
- Navegar por botón y táctil: `Tareas`, `Aprender`, `Biblioteca`, `Transferencia`, `Ajustes`.
- Abrir `Aprender` y comprobar entrada directa en `/Atlas-Aprender`.
- Abrir el EPUB 001, pasar páginas, volver al shell y conservar respuesta táctil.

## 30–60 segundos

- Sin cuelgue histórico a los 30 s; no debe iniciarse red ni OTA automáticamente.
- Abrir `Tareas`: selección táctil de filas, paginación por botones/swipe y contraste de tarea crítica.
- Ejecutar actualización manual del feed; comprobar cierre de Wi-Fi/worker al volver.
- Con Wi-Fi apagada, reentrar y comprobar último feed bueno con marca de caché.

## 60–120 segundos

- Suspender desde Home con caché válida: bloqueo Atlas con hora/fecha, actualización, conteo crítico y tarea prioritaria.
- Suspender sin caché válida/caché corrupta: fallback legible y sin crash.
- Despertar desde Home y desde lector; comprobar quick-resume y regreso correcto.
- Probar los modos de sleep heredados (claro, oscuro, custom/cover si están configurados) sin perder Atlas como shell.
- Mantener inactivo hasta 120 s y confirmar navegación inmediata.

## Rollback y persistencia

- Reinicio normal y reinicio forzado: vuelve a `1.5.10` solo si el health gate queda confirmado.
- Si falla cualquier gate de arranque/salud, rollback real a `1.5.9`.
- Verificar que permanecen Wi-Fi guardada, token/config Atlas, libros, progreso lector y SD.

## Aceptación

No publicar/promover `1.5.10` como estable hasta que todos los puntos anteriores hayan sido observados en el X4 físico. Registrar cualquier ghosting, clipping, latencia táctil o consumo anómalo como blocker de UX/energía antes de la siguiente iteración.
