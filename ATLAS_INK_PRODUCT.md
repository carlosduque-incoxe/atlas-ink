# Atlas Ink — contrato de producto y evolución

## Misión

Atlas Ink convierte el XTEINK X4 base completo en un **terminal e-ink programable de consulta y verificación para Carlos**. Atlas posee la portada, todos los menús, la navegación, la pantalla de reposo/bloqueo y el modelo de datos. La lectura de ebooks puede conservarse si aporta valor, pero ninguna estructura heredada del lector limita el producto.

El dispositivo está pensado para recibir atención ocasional: debe mostrar información útil y persistente sin depender de notificaciones, sonido ni vigilancia constante.

## Principios de producto

1. **Consulta antes que notificación.** La pantalla conserva el último estado útil; Carlos entra cuando quiere y actualiza cuando lo necesita.
2. **Información accionable y escasa.** Priorizar tareas críticas, vencimientos, bloqueos, agentes con incidencias y verificaciones rápidas. Evitar paneles densos o logs técnicos.
3. **Offline-first.** Cada módulo conserva un último estado válido con fecha y origen. Un fallo de red nunca deja la pantalla vacía ni bloquea la interfaz.
4. **Red fuera del loop principal.** Ninguna operación Wi-Fi/TLS/HTTP/OTA puede ejecutarse de forma síncrona dentro del loop de UI. Las primeras versiones usarán actualización manual; la automatización solo volverá tras una máquina de estados acotada y una prueba física de 30/60/120 segundos.
5. **Secretos fuera del firmware.** El X4 no almacena credenciales maestras de Vikunja, Microsoft, Home Assistant ni otros servicios. Consume feeds mínimos, de solo lectura y con token específico revocable cuando sea necesario.
6. **Autonomía práctica.** Objetivo mínimo: 24 horas de uso normal. Puede permanecer conectado y la eficiencia no prevalece sobre fiabilidad o utilidad.
7. **Evolución continua, no temeraria.** Atlas mantiene un bucle autónomo cada cuatro horas y puede rediseñar o implementar mejoras con libertad total de producto, pero una release solo se publica tras gates reales y las funciones con impacto físico se prueban por etapas.
8. **Identidad Atlas.** Interfaz monocroma, directa, legible y coherente con el símbolo Atlas Ink.
9. **Dispositivo completo.** No añadir módulos Atlas dentro de una interfaz de ebook para evitar molestar. Rediseñar el shell entero alrededor de consulta, calendario, tareas, agentes, estado y verificación.

## Atlas Shell

La navegación principal deja de ser la del lector y pasa a ser propiedad de Atlas:

- **Portada:** estado de hoy, tareas críticas, próxima cita, bloqueos y frescura de datos.
- **Hoy:** calendario, vencimientos y decisiones pendientes en una línea temporal compacta.
- **Tareas:** prioridades de Aerovía/Vikunja, navegación y detalle legible.
- **Agentes:** trabajos activos, bloqueados, terminados y resultados pendientes de revisar.
- **Estado:** salud resumida de servicios seleccionados, casa y trabajo; consulta por defecto, no control destructivo.
- **Consulta:** QR, notas, códigos, listas y respuestas preparadas por Atlas.
- **Aprender:** micro-libro semanal personalizado de unos cinco minutos, con una habilidad, esquema, ejercicio y práctica de siete días.
- **Ajustes:** red, actualización firmada, energía y diagnóstico del dispositivo.

La pantalla de reposo/bloqueo es una superficie de producto: conserva hora/fecha, próxima cita, tarea más crítica, bloqueos y antigüedad de los datos con consumo mínimo. No debe requerir red para dibujarse.

## Una cosa nueva que aprender

Atlas genera una entrega semanal a partir de las decisiones, fricciones y objetivos recientes de Carlos. No es contenido genérico ni una lista de consejos: cada EPUB desarrolla una sola habilidad de alto retorno, utiliza ejemplos discretos de su operativa, incluye un esquema monocromo, un ejercicio de dos minutos y una práctica medible para siete días.

Los libros se generan y validan fuera del X4, se conservan en una biblioteca versionada y se sincronizan sin sobrescribir archivos existentes. El formato EPUB es una capacidad de lectura; no devuelve el producto a una arquitectura ebook-first. La selección y la navegación pertenecen a Atlas Shell. La red permanece manual/no bloqueante y el libro ya cargado se abre completamente offline.

## Primer hito: Atlas Shell y Portada viva

La portada y la pantalla de reposo serán superficies de vistazo con:

- hasta 5 tareas críticas/próximas de Aerovía/Vikunja;
- prioridad, proyecto, vencimiento y estado breve;
- resumen de agentes: trabajando, bloqueados o con resultado pendiente;
- fecha/hora de la última actualización válida;
- indicador inequívoco de datos en caché o error de conexión;
- acción manual **Actualizar** y acceso a detalle paginado.
- próxima cita o bloque de calendario disponible;
- navegación directa al resto del shell Atlas, sin pasar por menús heredados de ebook.

La integración usará un feed compacto de dispositivo. El backend selecciona y reduce los datos; el X4 no descarga el tablero completo ni interpreta HTML arbitrario.

## Contrato inicial del feed

```json
{
  "schema": 1,
  "generated_at": "2026-08-18T20:00:00Z",
  "etag": "opaque-version",
  "tasks": [
    {
      "id": "NIL-8",
      "title": "Texto corto y legible",
      "project": "Nil",
      "priority": 4,
      "due": "2026-08-19T08:30:00Z",
      "state": "open"
    }
  ],
  "agents": [
    {
      "name": "Atlas correo",
      "state": "blocked",
      "summary": "Permiso de escritura pendiente"
    }
  ]
}
```

Límites previstos: respuesta <= 12 KiB, máximo 5 tareas y 5 agentes, textos normalizados a UTF-8 y longitudes acotadas. Los campos desconocidos se ignoran; un esquema futuro incompatible conserva la caché anterior.

## Roadmap vivo

### Fase A — plataforma segura

- [x] Bootstrap USB sin borrar datos.
- [x] OTA A/B firmada offline, digest e inspección ESP32-C3.
- [x] Rollback y health window.
- [x] Identidad y logo Atlas Ink.
- [x] OTA manual físicamente validada de 1.5.6 a 1.5.7.
- [x] Desactivar OTA automática bloqueante.
- [ ] Abstracción común de trabajos de red no bloqueantes.
- [ ] Caché atómica y versionada para módulos Atlas.

### Fase B — Portada Atlas

- [ ] Endpoint de feed reducido y autenticación de dispositivo de solo lectura.
- [x] Parser y tests de límites/truncación.
- [x] Actividad principal Atlas con caché y actualización manual.
- [x] Tareas críticas de Aerovía/Vikunja.
- [ ] Estado resumido de agentes/automatizaciones.
- [ ] QA físico 30/60/120 s, red caída, respuesta corrupta y rollback.

### Fase B2 — Atlas Shell completo

- [ ] Sustituir el menú principal heredado por Portada, Hoy, Tareas, Agentes, Estado, Consulta, Aprender y Ajustes.
- [ ] Convertir reposo/bloqueo en panel offline útil con calendario y tarea crítica.
- [ ] Definir feed agregado versionado para portada/calendario/agentes sin credenciales maestras.
- [x] Mantener lector únicamente como capacidad secundaria opcional.
- [ ] Verificar botones, táctil, despertar, reposo, batería y rollback en hardware real.

### Fase B3 — Aprendizaje semanal

- [x] Pipeline local reproducible para EPUB, portada y esquema monocromos.
- [x] Primera entrega validada: *Hoy, Semana o Fuera de la Cabeza*.
- [x] Generación semanal personalizada y cola de sincronización silenciosa.
- [ ] Integrar biblioteca **Aprender** como sección de primer nivel en Atlas Shell.
- [ ] Sincronización manual/no bloqueante desde manifiesto autenticado cuando el shell esté listo.
- [ ] Validar apertura, paginación, suspensión/reanudación y lectura offline en el X4 real.

### Fase C — módulos de consulta

Candidatos priorizados por utilidad real, no por cantidad:

- Estado Atlas: agentes, trabajos largos, bloqueos y últimos resultados.
- Hoy: agenda, vencimientos y decisiones pendientes.
- Casa/trabajo: verificaciones seleccionadas de Home Assistant, nunca control destructivo por defecto.
- Infra: salud resumida de servicios de Carlos y acciones recomendadas.
- Bandeja: correo realmente importante, sin marcar leído ni mover mensajes.
- Código/QR: mostrar enlaces, códigos breves o datos para continuar una acción en móvil/PC.
- Notas rápidas y listas de comprobación offline.

## Política del bucle continuo

Cada ejecución:

1. Lee este contrato, estado Git, última release, incidencias y feedback reciente disponible.
2. Escoge **una mejora de alto valor y alcance limitado** o un endurecimiento necesario.
3. Implementa y verifica de verdad cuando sea posible; no deja stubs ni botones vacíos.
4. Ejecuta formato, tests nativos, build local limpio e inspección del binario cuando toque firmware.
5. No usa GitHub Actions para construir o publicar.
6. Publica una versión patch firmada solo si todos los gates automáticos pasan y el cambio es recuperable.
7. Si requiere hardware, prepara candidato y criterios de aceptación; no afirma que está validado hasta recibir evidencia física.
8. Registra decisiones duraderas aquí o en documentación técnica, no acumula un diario ruidoso en memoria.

## Gates de release

- árbol comprometido y revisión de diff;
- tests relevantes verdes;
- `pio run -e gh_release -t clean` y build local exitosos;
- imagen final identificada como ESP32-C3, proyecto `atlas-ink`, versión correcta y dentro de partición;
- assets/bitmaps críticos presentes en el binario cuando aplique;
- manifest, SHA-256 y firma ECDSA offline verificados;
- descarga de la release publicada y verificación independiente;
- sin CI/Actions activos;
- ruta de rollback conocida;
- prueba física explícita para cambios de arranque, energía, red en background, particiones, botones o render principal.

## Límites de autonomía

Atlas puede diseñar, programar, probar, firmar y publicar mejoras recuperables. Debe detenerse antes de:

- borrar flash, NVS, libros o SD;
- cambiar bootloader, tabla de particiones o selector OTA sin autorización concreta;
- introducir credenciales maestras o datos sensibles en firmware/repo/release;
- reactivar tareas de red automáticas sin arquitectura no bloqueante y QA físico;
- publicar cambios que no compilan o cuya recuperación no está definida;
- afirmar comportamiento físico no probado.
