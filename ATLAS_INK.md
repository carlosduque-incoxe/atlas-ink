# Atlas Ink

Atlas Ink convierte el XTEINK X4 base (ESP32-C3) de Carlos en un terminal e-ink programable de consulta y verificación. Parte de CrossPoint Reader para conservar display, botones, SD, Wi-Fi, energía, recuperación y particiones OTA A/B ya probadas.

El contrato de producto y los gates de evolución están en [`ATLAS_INK_PRODUCT.md`](ATLAS_INK_PRODUCT.md).

## Estado operativo

- La primera instalación de Atlas Ink se hace por USB sin borrar bootloader, NVS, SD ni el backup stock.
- Las versiones posteriores se instalan manualmente desde `Ajustes -> Actualizar`.
- La comprobación OTA automática está deshabilitada: la implementación síncrona anterior bloqueó el loop principal a los 30 segundos en hardware real.
- No se reintroduce red automática hasta disponer de un diseño asíncrono con límites, cancelación y QA físico.
- Cada release exige firma ECDSA P-256 offline, SHA-256, imagen ESP32-C3 válida, A/B, health check y rollback.
- Build, tests, firma y publicación son locales. GitHub CI no forma parte del flujo normal de Atlas Ink.

## Seguridad OTA

1. El firmware consulta releases de `carlosduque-incoxe/atlas-ink` solo cuando Carlos inicia la actualización.
2. Exige `firmware.bin`, manifiesto y firma de la clave pública fijada en el firmware.
3. Valida semver, tamaño, SHA-256, firma ECDSA, formato de imagen y chip antes de seleccionar el slot inactivo.
4. Registra slot anterior/nuevo en NVS con integridad.
5. La versión nueva solo se confirma tras la ventana de salud; un fallo vuelve al slot anterior.
6. La clave privada permanece fuera del repo y de GitHub.

## Portada Atlas

La primera función de terminal programable es una portada manual para tareas importantes de Aerovía/Vikunja:

- menú principal `Atlas`;
- hasta cinco tareas abiertas con prioridad >= 3;
- orden por prioridad, fecha límite e ID;
- resumen de agentes reservado para el mismo feed;
- última copia válida visible sin Wi-Fi;
- actualización solo al pulsar `Actualizar`;
- worker con timeout, límite de 12 KiB, JSON acotado y sin redirects;
- caché temporal + backup + rename;
- el binario no contiene tokens.

### Provisioning

El archivo privado es `/.crosspoint/atlas.json` en la SD:

```json
{
  "url": "http://10.10.1.111:3456/api/v2/atlas-ink/feed",
  "token": "TOKEN_DE_SOLO_ATLAS_INK_FEED"
}
```

En el primer arranque, el firmware valida URL/token y reescribe el token en formato ofuscado con integridad ligada al hardware. El archivo real no se añade al repo ni se envía por mensajería.

La versión inicial acepta únicamente HTTP hacia IPv4 privadas RFC1918. Es una restricción deliberada: el backend wolfSSL actual no valida la identidad TLS. El token es dedicado, revocable y solo tiene permiso `atlas_ink.feed`; HTTPS no se habilitará hasta añadir CA o pinning real.

## Disciplina de release

1. Partir de revisión comprometida y árbol limpio.
2. Formatear y ejecutar toda la suite nativa.
3. Compilar localmente `gh_release` y verificar tamaño, partición e imagen ESP32-C3.
4. Firmar offline y publicar binario, manifiesto y firma.
5. Descargar la release publicada y verificar firma/digest de nuevo.
6. Probar manualmente en hardware: arranque, botones/táctil, 30/60/120 s, refresco, caché sin red y rollback.
7. No promover cambios de red/energía sin QA físico explícito.

El backup completo stock sigue siendo privado y no forma parte del repositorio.
