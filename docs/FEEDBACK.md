1. Gameplay & Mecánicas del Mundo
Animales e Inteligencia Artificial
Ataques: Los animales siguen sin poder atacarse entre sí o al jugador.

Selección y Ciclo de Vida: Los animales no son seleccionables; existe un posible bug vinculado a la imposibilidad de que mueran.

Recolección de Recursos y Materias Primas
Recolección manual: Permitir recoger materias primas a mano (con penalización de tiempo y menor rendimiento de recursos).

Bloqueo con rocas y herramientas: Si la roca exige pico obligatoriamente para minarse, impide fabricar un primer pico si no se tienen materiales básicos.

Nodos de recolección: Añadir nodos menores en el mapa (ej. montones de piedras pequeñas), siguiendo la misma lógica que con los árboles.

Descanso del Jugador
Mecánica de descanso: Estudiar e implementar una forma para que el personaje descanse (ej. mediante inactividad/no hacer nada o reposo activo).

2. Construcción & Sistema de Trabajo
Sistema de Construcción y Clan
Previsualización de ubicación: Al seleccionar un edificio, debe desplegarse una vista previa (preview) para elegir libremente su posición en el mundo.

Autonomía del jugador: Evitar el flujo actual donde el clan opina o descarta la construcción; el jugador debe poder forzar y continuar la orden libremente.

Corrección de BUGS Críticos
Bloqueo de edificación (Shelter 1): Error al intentar finalizar una construcción. Tras reparar la primera fase, se produce el fallo:failed: the target 'Shelter 1' needs Building.Ready
El proceso se bloquea por completo y deja de funcionar.

3. UI / UX & Interfaz del Juego
Barras de Progreso e Indicadores
Progreso de reaparición (Regrow): Sustituir o mejorar los menús de seleccionables usando barras de progreso visuales.

Progreso de acciones: Añadir una barra de progreso visible cuando el personaje realiza una acción en tiempo real.

Control y Navegación
Cancelación: Añadir el botón/comando para cancelar la tarea activa actual.

Localización: Traducir toda la interfaz y los textos del juego al español.

4. Editor de Motor & Herramientas de Desarrollo
Jerarquía y Navegación del Escenario
Resaltado visual (Outline): Al seleccionar una entidad en la escena, debe iluminarse con un outline junto con los elementos de su jerarquía. El elemento correspondiente debe quedar marcado en el panel Hierarchy.

Enfoque de cámara: Hacer clic en un elemento del panel Hierarchy debe desplazar automáticamente la cámara hasta la posición de dicho objeto.

Menú de Configuración de Edificios (Modo Editor)
Gestión de requisitos e interacción: Implementar un menú para configurar y previsualizar requisitos de construcción, estadísticas, puntos/nodos de interacción e información interna de las estructuras.

Modo Vista Previa (Preview): Crear un entorno visual dentro del editor (similar al de Animation Bindings) para examinar:

Construcciones y sus fases de desarrollo.

Materiales requeridos y aporte socioeconómico.

Secuencia de montaje en modo preview.

Persistencia: Incluir funcionalidad para guardar los datos configurados en el editor y consumirlos en runtime durante la partida.

5. Sistema de Animación & Animation Bindings
Funcionamiento Independiente y Preview
Entorno de pruebas autónomo: El menú debe operar de manera independiente para validar y ajustar las transiciones correctamente.

Previsualización e interacción: Debe permitir cargar al personaje, inspeccionar nodos, configurar las zonas interactivas para asociar animaciones a acciones, y ajustar las posiciones de los nodos tanto en objetos construibles como en recursos recolectables del mundo.

Reproducción: Habilitar la previsualización completa de la animación (reproducción continua de inicio a fin).

Refactorización y Arquitectura (Timeline / Animation Bindings / Animation Graph)
Objetivo de testeo: Probar la integración de cada animación con su tarea correspondiente en el mapa, verificando puntos de contacto e interacción.

Limpieza de código: Analizar el código actual repartido en los 3 menús (Timeline, Animation Bindings y Animation Graph), eliminando duplicidades y código obsoleto.

Arquitectura: Rediseñar la estructura para obtener un sistema desacoplado, fácilmente iterable y que cumpla los principios SOLID.

6. Rendimiento & Debugging
Física y Colisiones
Revisión de colisiones: Auditoría general de colliders y comportamiento físico en escena.

Visualizador de Colliders: Añadir un comando de consola o atajo para renderizar y visualizar las mallas de colisión (debug draw).

Optimización y Caída de FPS
Degradación con el tiempo: El rendimiento cae progresivamente tras un tiempo de ejecución hasta alcanzar ~20 FPS.

Herramientas de perfilado: Desarrollar un menú/panel de debugging técnico de rendimiento en tiempo real para diagnosticar fugas de memoria, cuellos de botella (draw calls, ciclos de CPU/GPU) o acumulación de entidades.

7. Bugs

- Jugador: al recoger bayas se ha quedado el personaje bloqueado en estado running pero sin hacer nada haciendo un hard block del juego. 