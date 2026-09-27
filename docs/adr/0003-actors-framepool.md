# ADR-0003: Actores con dueño único y FramePool con referencias contadas

- Estado: aceptada · 2026-09-27

## Contexto
Bugs B4, B7–B10 de v1.1.2: semáforos no liberados, frame buffer del driver leído por la web
mientras otra tarea lo devolvía, sensor reconfigurado en paralelo a una captura.

## Decisión
Cada recurso (cámara, red, SD, cliente Connect) pertenece a una tarea que procesa una cola de
mensajes. Los frames se copian a un `FramePool` de slots pre-reservados en PSRAM y se comparten
como `FrameRef` inmutables con conteo atómico; el `fb` del driver se devuelve inmediatamente.
Los handlers HTTP no llaman drivers: envían un mensaje con timeout o leen el último `FrameRef`.

## Consecuencias
+ Sin locks en el camino de lectura; imposible liberar un frame en uso.
+ Sin heap en caliente (slots fijos).
− Una copia extra por foto (~20–150 KB a PSRAM, < 5 ms): aceptable a 1 foto cada 10 s o más.
