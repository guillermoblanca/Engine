# Plan: un mundo vivo de clanes, de la recolección a la era espacial

> Documento de planificación. Recoge la visión del juego, lo que el motor ya tiene, los principios de diseño, los hitos en orden de dependencia (con entregables, criterios de aceptación y riesgos), la matriz que comprueba que cada requisito está cubierto, los carriles de trabajo en paralelo y los primeros pasos.
> Estado de partida: `master` con locomoción mezclada, montajes, sockets e IK, interacciones, ragdoll, retargeting, editor y QA automatizado. En curso: huerto con sistema de habilidades tipo GAS (rama `feature/huerto-gas`) y árboles de animación (otro agente).

[← Volver al README](../README.md)

## 1. La visión

Un mundo persistente en el que **clanes de personajes no jugadores** viven por sí solos y el jugador es un miembro más:

- Los clanes empiezan en puntos distintos del mapa. Encuentran y usan recursos, construyen sus casas y se asientan, exploran y descubren cosas nuevas.
- Cada clan acumula **conocimiento y tecnología propios**, que difunde entre sus miembros y que puede perder o intercambiar.
- Los personajes tienen **hijos que heredan** habilidades, rasgos y estadísticas; hay parentesco, relaciones entre agentes y un **rol** que cada uno cumple para la sociedad.
- Los clanes **dialogan**: comercio, alianzas, guerra y disputas por el territorio, según el carácter de sus integrantes. Una sociedad se vuelve **violenta o pacífica** por la suma de sus miembros.
- El jugador **se asocia o se integra** en un clan y participa de su vida.
- Todo avanza con el tiempo, **se guarda** y mantiene una simulación compleja. Las eras llegan desde la agricultura y el asentamiento hasta la **era espacial**.
- Es la misma lógica para jugador y NPC: las habilidades, las interacciones y las animaciones no distinguen quién las ordena.

El volumen de animaciones que ya tenemos (locomoción, granja, heridos, magia, espada y escudo, arco, gestos) es lo que permite que el mundo se vea rico desde las primeras eras.

## 2. Lo que el motor ya tiene (y lo que falta)

| Área | Ya existe | Falta para este plan |
|---|---|---|
| **Mundo** | Terreno, ríos, lagos, rocas, bosques con biomas, hierba, agua, clima, ciclo día/noche, física, destrucción | Ecología (crecimiento, estaciones, agotamiento), recursos como entidades persistentes, mapa grande por regiones |
| **Personaje** | Controlador en tercera persona, locomoción mezclada con sets, giros en sitio, salto, montajes y slots, sockets, props con IK, IK de pies, mirada, ragdoll, retargeting, alta automática de personajes | Árboles de animación (otro agente), sets de espada, arco, magia, agachado; reacciones y muertes como habilidades |
| **Interacción** | `InteractableComponent`, `InteractionRunner`, `MotionWarp`, `TaskRunner`, `AIDriver` (línea recta con detección de atasco), entidades con jerarquía y escenas JSON | Navegación real, percepción, memoria, cerebro de decisión |
| **Gameplay** | Sistema de habilidades tipo GAS (rama `feature/huerto-gas`): etiquetas, atributos, efectos, habilidades, componente | Inventario, objetos, recetas, construcción, economía |
| **Sociedad** | Nada | Todo: clanes, demografía, genética, relaciones, tecnología, diplomacia, conflicto |
| **Persistencia** | Escenas JSON versionadas de entidades; golden data | Guardado del estado de la simulación, versiones, deltas |
| **Escala** | Un mundo de 360 m | Regiones, LOD de simulación y de animación, miles de agentes abstractos |
| **Herramientas y QA** | Editor con paneles, consola y guiones sin ventana; golden de imágenes y datos; barridos de semillas | Inspectores de sociedad, editor de datos, QA de simulación (balance e invariantes) |

## 3. Principios de diseño (todos los hitos los respetan)

1. **Una sola lógica para jugador y NPC.** Una orden (jugador o IA) produce el mismo `CharacterInput` o activa la misma habilidad. Ninguna regla de juego vive en el controlador del jugador.
2. **Simulación separada de la presentación.** La sociedad se simula sin GL, con un tic fijo y datos puros; lo visible (animación completa, física, sonido) es una capa por niveles de detalle encima. Todo se ejecuta sin ventana.
3. **Determinista.** Misma semilla y mismas órdenes, mismo mundo. Estado numérico crítico con aritmética controlada (enteros, punto fijo o `double` sin funciones trascendentes dependientes de la plataforma); aleatoriedad con generadores propios con semilla por sistema. Es lo que permite pruebas, repeticiones y guardado fiable.
4. **Guiado por datos.** Eras, tecnologías, recursos, recetas, rasgos, roles, edificios, habilidades y sets de animación se describen en archivos de datos, no en C++. Añadir una era o un recurso no recompila el motor.
5. **Niveles de detalle de simulación.** Cerca de la cámara, el personaje vive completo (animación, física, IK); lejos, es un agente abstracto que avanza con reglas simples y se reconstruye al volver. El coste crece con lo visible, no con la población.
6. **Guardar es parte de cada sistema.** Cada módulo define su estado serializable y su versión desde su primer día. No se añade un sistema sin su guardado y su test de ida y vuelta.
7. **Cada hito termina con una demo ejecutable y una prueba.** Una demo sin ventana con guion (`--script`) y un test de CPU determinista, más una captura para revisar con los ojos. Nada queda "casi hecho".
8. **Contenido por eras como paquetes de datos.** El motor se escribe para la era 1 y se prueba con la siguiente; las eras posteriores se amplían con datos y recursos gráficos pedidos al artista.

## 4. Hitos

Orden de dependencias; tamaños relativos (**S** una sesión de trabajo, **M** unas pocas, **L** varias, **XL** una serie). Cada hito lista su objetivo, sus entregables, el criterio de aceptación y los riesgos. Los hitos H0 a H4 se llaman la **base**; H5 a H8, los **agentes**; H9 a H12, la **sociedad**; H13 y H14, la **diplomacia y el conflicto**; H15 y H16, el **jugador y la escala**; H17 y H18, las **eras y la economía**; H19 y H20, la **calidad y las herramientas**.

### Base de juego

#### H0 · Consolidación y contrato de actor · S–M
**Objetivo.** Dejar el sistema de animación e interacción en un estado estable que los demás hitos puedan usar sin romperse.
**Entregables.**
- Fusionar `feature/huerto-gas` y el trabajo de árboles de animación del otro agente.
- **Manifiesto de sets y montajes** (datos): para cada pack, qué clips cumplen qué papel (idle, andar, correr, strafes, giros, saltos, agachado, ataques, bloqueos, reacciones, muertes, gestos). Sustituye las listas escritas en C++ (`MixamoSets`, nombres de clips en `CharacterAnimator`).
- **Contrato de actor** (`Actor`): lo mínimo que comparten jugador y NPC: identidad, habilidades concedidas, atributos, inventario, modelo animado, controlador y fuente de órdenes (teclado, IA, guion).
- Registrar los packs nuevos como sets: `crouch`, `sword_shield`, `bow_aim`, `magic`, y los montajes de gestos y reacciones.
**Estado (cerrado).** El contrato de actor es `GB::Actor` (`Engine/src/GB/Game/Actor.h`): identidad (nombre y entidad de la escena), habilidades (el sistema de la entidad del mismo nombre), modelo animado, controlador y **fuente de órdenes intercambiable** (`OrderSource`: entrada directa de teclado o mando, IA, lista de tareas, guion, repetición). `ActorWorld` crea, actualiza, dibuja y elimina muchos actores, comparte el `Model` entre los que lo usan y hace que cada actor trate a los demás como obstáculos. El Editor lo usa con `actor spawn|list|control|tasks|remove` y `PlayerMode` es solo un actor controlado más la cámara y el teclado. Los atributos van en el sistema de habilidades (H1) y el inventario llegará con H2. Decisiones: cada actor lleva su copia de los obstáculos estáticos y su `PhysicsWorld` de ragdoll bajo demanda (se compartirán en H16); un actor cuesta ~45 µs por fotograma, ver `docs/COSTE_ACTOR.md`.
**Aceptación.** `character_check` marca `ready` con los siete packs; los tests de sets y montajes pasan sin listas de nombres en C++; el mismo actor se maneja con teclado, con `player_tasks` y con un guion.
**Riesgos.** Solaparse con el trabajo de árboles de animación: se coordina el formato del manifiesto antes de escribirlo.

#### H1 · Núcleo de habilidades (GAS) · M
**Objetivo.** Habilidades y efectos como la capa que decide qué puede hacer un actor, igual para jugador y NPC.
**Entregables.** Etiquetas jerárquicas, atributos con modificadores, efectos (instantáneos, con duración y periódicos), habilidades con requisitos, coste y enfriamiento, tareas de habilidad (reproducir montaje, esperar evento, ir a un objetivo), componente de habilidades por entidad, guardado y carga. Los efectos de un montaje se aplican con sus notificaciones. **En curso** en `feature/huerto-gas`.
**Aceptación.** Tests deterministas de etiquetas, atributos, efectos, coste y enfriamiento; un actor activa la misma habilidad desde teclado y desde IA con el mismo resultado.
**Riesgos.** Hacer un clon completo de GAS: se mantiene lo mínimo útil y se amplía por necesidad.

#### H2 · Objetos, inventario y recursos del mundo · M
**Objetivo.** Que haya cosas que recoger, llevar, guardar, usar y transformar.
**Entregables.**
- Definición de objeto en datos (nombre, peso, apilado, categoría, etiquetas, qué habilidad lo usa).
- Inventario y contenedores (mochila, caja, almacén), con agarre visible por socket.
- **Nodos de recurso** como entidades: árbol, roca, yacimiento, baya, agua, presa, con cantidad, regeneración y agotamiento.
- Recetas y estaciones de trabajo (datos): materia prima → herramienta u objeto, con habilidad y montaje.
- Montajes que faltan (recoger del suelo, dejar, cavar en bucle): ver el apartado 7.
**Aceptación.** Un actor tala, mina, recoge y fabrica una herramienta de principio a fin, con el mismo resultado en dos ejecuciones; el inventario se guarda y se carga idéntico.
**Riesgos.** Explosión de objetos y datos: se parte de un puñado de recursos de la era 1.

#### H3 · Mundo interactivo y ecología · M–L
**Objetivo.** Un entorno controlado con interactivos que se amplían reutilizando la misma lógica.
**Entregables.**
- Interactivos en datos: parcela (hierba, cavada, sembrada, brote, crecida, regada), árbol frutal, vaca, cajas, carretilla, puertas, fogata, depósitos de agua.
- **Estado persistente de entidad** con máquina de estados y temporizadores del mundo.
- Ciclo de las plantas con riego, estaciones y clima (reutiliza `Weather`), regeneración de recursos y ecología básica (animales que se mueven, comen y se reproducen).
- Editor: panel de interactivos y de estados; comando para colocarlos.
**Aceptación.** Un huerto completo (cavar, sembrar, regar, crecer, recoger) y un bosque que se regenera, con simulación acelerada comprobada por test; un NPC con `player_tasks` hace lo mismo que el jugador.
**Riesgos.** Cantidad de estados a vigilar: pruebas de invariantes (ningún recurso negativo, ningún estado huérfano).

#### H4 · Persistencia v1 · M
**Objetivo.** Guardar y cargar el mundo y todos sus estados desde el primer momento, y repetir una partida.
**Entregables.** Formato de guardado versionado (cabecera, versión, semilla, reloj de simulación, entidades con componentes, estados de interactivos, inventarios, habilidades), con **migraciones** entre versiones; guardado rápido y autoguardado; guardado diferencial sobre el mundo generado por semilla (solo lo que cambió); grabación de órdenes para repetir una partida y detectar regresiones.
**Aceptación.** Guardar, cargar y guardar produce el mismo archivo; cargar una partida de la versión anterior migra y funciona; la repetición de una partida da el mismo estado final.
**Riesgos.** Compatibilidad hacia atrás: tests con partidas guardadas de versiones antiguas guardadas en el repositorio.

### Agentes

#### H5 · Navegación y percepción · L
**Objetivo.** Que un NPC encuentre solo el camino y sepa lo que ve.
**Entregables.**
- **Navegación sobre el terreno:** malla de navegación o cuadrícula con costes (pendiente, agua, vegetación, edificios), pathfinding con evitación local, caminos por regiones para largas distancias, costes por rol y carga.
- `AIDriver` sustituido por un seguimiento de camino con la misma interfaz de entrada que el jugador.
- **Percepción:** vista (cono y distancia) y oído; memoria espacial por agente y por clan (qué recursos conoce, dónde están, cuándo los vio).
- Depuración visual en el editor (rutas, conos, memoria).
**Aceptación.** 50 NPC llegan a puntos de recursos sin atascarse, esquivan árboles, rocas y otros NPC, rodean el agua o la vadean según el coste; coste de CPU medido y dentro de presupuesto.
**Riesgos.** Rendimiento de la navegación y su actualización cuando el mundo cambia (edificios, árboles talados): actualización por regiones.

#### H6 · Necesidades y cerebro de NPC · L
**Objetivo.** Que cada NPC decida qué hacer según lo que necesita y lo que sabe.
**Entregables.**
- **Necesidades:** hambre, sed, descanso, abrigo, seguridad, compañía, con curvas y efectos sobre atributos.
- **Motor de decisión** (IA de utilidad, o planificación por objetivos si lo exige el contenido): elige una habilidad, un objetivo y un lugar, y lo ejecuta con las habilidades de H1 y las tareas de H5. Agenda diaria (día y noche, estaciones).
- **Nivel de detalle de simulación:** el actor completo cerca de la cámara y el agente abstracto lejos, con la misma decisión; paso de uno a otro sin saltos.
- Inspector de NPC en el editor: estado, necesidades, decisión actual y por qué.
**Aceptación.** 100 NPC aislados durante 10 días simulados sobreviven en un mundo con recursos suficientes y mueren de hambre en uno sin ellos; el reinicio desde un guardado continúa igual.
**Riesgos.** Comportamiento oscilante o bobo: ajuste por pruebas de balance con barridos de semillas.

#### H7 · Roles y trabajo del clan · M–L
**Objetivo.** Que cada personaje contribuya a la sociedad según sus aptitudes y las necesidades del grupo.
**Entregables.** Roles en datos (recolector, cazador, agricultor, leñador, constructor, artesano, guardia, curandero, líder, explorador), aptitudes por rol, **tablón de trabajos del clan** (necesidades de recursos y obras que se publican como trabajos), asignación y cambio de rol con los datos del clan, eficiencia según habilidad y fatiga, aprendizaje con la práctica.
**Aceptación.** Un clan de 20 se organiza solo: alguien siembra, alguien recoge, alguien construye, sin que ninguna categoría de trabajo quede desatendida más de un día; cambia la distribución cuando cambia la necesidad.
**Riesgos.** Atascos del tablón (nadie quiere un trabajo): prioridades y penalizaciones.

#### H8 · Construcción y asentamientos · L
**Objetivo.** Que los clanes elijan sitio, construyan sus casas y crezcan.
**Entregables.**
- **Elección de lugar** con una puntuación (agua, recursos cercanos, defensa, suelo llano, riesgos), fundación y expansión.
- **Construcción por fases** con planos (datos): cimientos, estructura, cubierta; consume materiales del almacén; animaciones de obra; las obras se ven crecer.
- Tipos: refugio, casa, almacén, campo, corral, hogar, pozo, muralla y otros según la tecnología (H11).
- **Territorio** del clan: mapa de influencia y límites.
- Edificios con interior mínimo (descanso, almacenaje) y puntos de interacción para los NPC.
**Aceptación.** Un clan fundado en un punto aleatorio de cinco semillas distintas construye un asentamiento con al menos refugios, almacén y campo en 30 días simulados; la semilla y el guardado reproducen exactamente la misma disposición.
**Riesgos.** Colocación en terreno irregular y colisiones con árboles y rocas: reglas de despeje y nivelación del suelo.

### Sociedad

#### H9 · Demografía y herencia · L
**Objetivo.** Que haya nacimientos, crecimiento, vejez y muerte, y que los hijos hereden de sus padres.
**Entregables.**
- **Ciclo de vida** (niño, joven, adulto, mayor), embarazo y nacimiento, parentesco, muerte natural, por enfermedad, por hambre o por violencia.
- **Genética**: atributos y aptitudes heredables (fuerza, resistencia, inteligencia, agresividad, sociabilidad, longevidad, talentos), mezcla de los progenitores con mutación y variación controlada; **rasgos** que se ven.
- Rasgos visibles en el modelo (tamaño, proporciones, tono), con escalado de esqueleto y retargeting (ya existe) para niños y adultos.
- Educación: los hijos aprenden de sus padres y del clan (conecta con H11).
**Aceptación.** Tras 200 años simulados, una población parte de 30 y se mantiene, crece o se extingue según los recursos; la herencia de un rasgo a lo largo de generaciones cumple la estadística esperada (probada con barridos de semillas); los guardados de población son idénticos al reanudar.
**Riesgos.** Modelos y animaciones para niños y mayores (apartado 7); estabilidad numérica de poblaciones a largo plazo.

#### H10 · Relaciones, personalidad y cultura · L
**Objetivo.** Que los agentes se conozcan, se quieran o se odien, y que eso decida lo que hacen.
**Entregables.** Grafo social (afinidad, confianza, parentesco, rivalidad, deudas), rasgos de personalidad, estado de ánimo y emociones, memoria de eventos (ayudas, agravios, pérdidas), normas y valores del clan (pacifismo, ambición, respeto a los mayores), liderazgo y su sucesión, conversación entre NPC con gestos (reutiliza el pack de gestos), pareja y familia.
**Aceptación.** Eventos controlados (un robo, un rescate, una muerte) cambian las relaciones del grafo con el resultado esperado y se reflejan en el comportamiento; el clan con un líder agresivo y uno pacífico se desvían en un barrido de semillas con la misma configuración.
**Riesgos.** Complejidad: mantener pocas dimensiones y bien medidas.

#### H11 · Conocimiento y tecnología · L
**Objetivo.** Que cada clan descubra, aprenda, enseñe y olvide.
**Entregables.**
- **Árbol tecnológico en datos** por eras: cada tecnología con requisitos (otras tecnologías, recursos, edificios, habilidades) y qué desbloquea (recetas, edificios, habilidades, rasgos).
- **Descubrimiento**: por experimentación, observación del entorno, exploración y accidente; probabilidades y coste de tiempo; estado de conocimiento por clan (y por individuo).
- **Difusión**: enseñanza dentro del clan (maestros y aprendices, tradición oral), y entre clanes (comercio, alianza, espionaje, conquista).
- **Pérdida**: sin practicantes o por catástrofe, el conocimiento puede perderse.
- Vista del árbol por clan en el editor.
**Aceptación.** Dos clanes en mundos iguales con semillas distintas siguen caminos distintos y llegan a tecnologías distintas; el conocimiento pasa de un clan a otro por comercio; una tecnología se pierde cuando muere el último que la sabe.
**Riesgos.** Balance y ritmo (que la era 2 no llegue en 10 días ni en 10 000): barridos de semillas y curvas objetivo.

#### H12 · Exploración, migración y descubrimiento de recursos · M
**Objetivo.** Que los clanes recorran el mundo y lo vayan conociendo.
**Entregables.** Mapa de conocimiento por clan, **expediciones** (explorar una región, buscar un recurso, fundar un nuevo asentamiento), migración por escasez, riesgos del camino (animales, clima, otros clanes), puntos de interés descubiertos y compartidos.
**Aceptación.** Un clan sin cierto recurso envía una expedición que lo encuentra y lo incorpora; un grupo funda un asentamiento nuevo y mantiene el contacto con el original.
**Riesgos.** Mundo grande y lejanía (ver H16).

### Diplomacia y conflicto

#### H13 · Clanes y diplomacia · L
**Objetivo.** Que los clanes se relacionen entre sí de forma emergente y legible.
**Entregables.** Entidad **clan** (miembros, territorio, recursos, política), actitud entre clanes derivada de sus miembros, encuentros y diálogo (propuestas de **comercio, alianza, tregua, tributo, matrimonio**), **comercio** (valoración, rutas y caravanas, necesidades y excedentes), reputación, tratados con duración y su ruptura, mensajeros.
**Aceptación.** Dos clanes cercanos acaban comerciando cuando se necesitan y se hacen aliados o enemigos según el carácter de sus miembros, con barridos de semillas que muestran las dos salidas.
**Riesgos.** Que la diplomacia sea una caja negra: registro de decisiones y explicación en el inspector.

#### H14 · Conflicto, combate y violencia · L–XL
**Objetivo.** Guerra, escaramuzas y defensa por territorio y recursos, con consecuencias.
**Entregables.**
- Causas de conflicto (escasez, venganza, ambición, agravio), decisión de atacar o ceder según el carácter y la fuerza relativa, **moral** y retirada.
- **Combate** con habilidades GAS: ataques de espada y escudo, arco, magia; bloqueo, esquiva, reacciones a golpes, heridas (set `injured`), muertes y **ragdoll**; armas y armaduras desde las recetas.
- Heridos, curación, bajas, botín, rendición y paz con condiciones.
- **Violencia emergente**: un clan con muchos agresivos, hambrientos y bien armados es más propenso a la guerra que uno pacífico.
- Escala: escaramuzas visibles (decenas) y batallas resueltas de forma abstracta (cientos) con resumen y reproducción corta.
**Aceptación.** En una repetición determinista, un clan agresivo ataca a uno débil y vecino y gana; uno pacífico y rodeado negocia; en un barrido, la tasa de guerra sube con la agresividad media.
**Riesgos.** Tono y legibilidad; equilibrio; mucho trabajo de animación (apartado 7).

### Jugador y escala

#### H15 · El jugador dentro de la sociedad · L
**Objetivo.** Que el jugador sea un miembro más y pueda cambiar el rumbo.
**Entregables.** Unirse, crear o abandonar un clan, rol del jugador y reputación, **trabajos y misiones que salen del tablón del clan** (no escritas a mano), diálogo, comercio, **órdenes** si es líder, interfaz de clan (miembros, relaciones, recursos, tecnología, territorio), decisiones con consecuencias para el resto del mundo.
**Aceptación.** El jugador entra en un clan, recibe trabajos, los cumple, sube de reputación y un cambio suyo (p. ej. descubrir un recurso o negociar la paz) cambia el estado de los demás.
**Riesgos.** Que el jugador sea el único que importa o que no importe nada: pruebas de influencia.

#### H16 · Escala, rendimiento y mundo grande · XL (transversal)
**Objetivo.** Cientos de agentes cerca y miles lejos, en un mundo de kilómetros, sin tirones.
**Entregables.**
- **Regiones** del mundo (generación y carga por zonas), streaming de terreno, objetos y agentes.
- **Niveles de detalle de animación**: pose completa cerca, menos huesos o menor frecuencia a media distancia, instancias baratas lejos.
- **Simulación abstracta** de agentes lejanos y **recuperación al volver**; simulación acelerada (aceleración de tiempo) y **puesta al día** de lo sucedido en la ausencia.
- Presupuestos de CPU y de memoria por sistema, medidos en cada hito; paralelismo donde convenga.
**Aceptación.** 200 agentes completos y 5000 abstractos con una tasa estable y sin tirones; la simulación a 100× el tiempo real produce el mismo mundo que a 1×.
**Riesgos.** Es lo que más puede cambiar la arquitectura: se empieza a medir desde H5 y se reserva una revisión a mitad de la serie.

### Eras y economía

#### H17 · Eras como paquetes de datos · XL (continuo)
**Objetivo.** Que el juego avance desde la agricultura hasta la era espacial sin tocar el motor.
**Entregables.** Formato de **paquete de era** (tecnologías, recursos, recetas, edificios, habilidades, rasgos, animaciones y modelos que usa), carga y validación de paquetes, y los contenidos por era:

| Era | Idea | Qué se añade |
|---|---|---|
| 1 · Recolección y caza | Los clanes sobreviven | Refugios, fuego, herramientas de piedra, caza y recolección |
| 2 · Agricultura y asentamiento | Primer huerto | Cultivos, ganadería, corrales, cerámica, graneros, aldeas |
| 3 · Metal | Herramientas y armas mejores | Minería, fundición, bronce y hierro, comercio a distancia |
| 4 · Antigua y medieval | Ciudades y estados | Murallas, caminos, mercados, escritura, leyes, ejércitos |
| 5 · Industrial | Máquinas y energía | Vapor, fábricas, ferrocarril, urbanización, contaminación |
| 6 · Moderna | Redes y servicios | Electricidad, motores, medicina, comunicaciones |
| 7 · Espacial | Más allá del mundo | Cohetes, estaciones, colonias en otro cuerpo (otro mapa), recursos fuera del planeta |

**Aceptación.** Cada era llega como un paquete que se instala y se prueba solo: sus tecnologías son alcanzables desde la anterior, todas sus recetas tienen materias primas alcanzables y sus edificios, animaciones y modelos existen. Validador que falla si algo está colgado.
**Riesgos.** Es la parte con más trabajo de contenido y arte: se avanza era por era, y cada una se entrega cuando es jugable, no entera de golpe.

#### H18 · Economía y producción compleja · L
**Objetivo.** Cadenas de producción, especialización, comercio y logística.
**Entregables.** Cadenas de producción multietapa, **especialización** y oficios, mercados y precios que salen de la oferta y la demanda, moneda y crédito (cuando la era lo permita), infraestructura (caminos, puentes, canales) y logística, energía, ciencia como actividad, impacto ambiental (deforestación, contaminación, agotamiento).
**Aceptación.** Una economía de varios clanes se equilibra o entra en crisis según sus recursos, con un barrido de semillas que lo muestra; los precios responden a una sequía simulada.
**Riesgos.** Estabilidad de modelos económicos: pruebas de invariantes y topes.

### Calidad y herramientas

#### H19 · Cámaras y calidad gráfica · L
**Objetivo.** Fluidez y buen aspecto con tanta población. (Corresponde a la fase que has planteado después de interacción y animación.)
**Entregables.** Cámaras de juego (tercera persona pulida, vista de estrategia o panorámica, cinemáticas y escenas de diplomacia), calidad por niveles, instanciado y LOD de personajes y vegetación, sombras y luces para muchos actores, iluminación de interiores, rendimiento en las configuraciones objetivo.
**Aceptación.** Metas de fotogramas por segundo y de tiempos de fotograma medidas en escenas de referencia (aldea, ciudad, batalla); capturas de referencia por plataforma.
**Riesgos.** Las imágenes de referencia dependen de la GPU: referencias por plataforma (ya existentes).

#### H20 · Herramientas, depuración y QA de simulación · L (transversal)
**Objetivo.** Poder entender y equilibrar un mundo complejo.
**Entregables.** Inspectores de clan, de agente, de relaciones y de tecnología; gráficos de población, recursos y economía en el tiempo; editor de datos (tecnologías, rasgos, roles, recetas, eras) con validación; **barridos de semillas de la simulación** con invariantes y métricas objetivo (población, guerras, tecnologías por año), informes y comparación entre versiones; registro de decisiones de la IA; documentación por sistema.
**Aceptación.** Cada hito añade sus métricas al barrido; una regresión de balance falla en CI; un diseñador cambia una tecnología sin tocar código.
**Riesgos.** Pruebas lentas: barridos nocturnos aparte de los tests de cada PR.

## 5. Matriz de cobertura de la visión

Cada frase de lo que has pedido, con el hito que la cubre. Nada queda sin hito.

| Requisito | Hitos |
|---|---|
| NPC que pertenecen a un clan | H7, H13 |
| Empiezan en puntos distintos del mapa | H8, H12, H16 |
| Encuentran y usan recursos del entorno | H2, H3, H5, H6 |
| Generan sus casas | H8 |
| Navegan por el mundo descubriendo recursos | H5, H12 |
| Métodos para avanzar tecnológicamente | H11, H17 |
| Aprendizaje propio de cada tribu | H11 |
| Tienen hijos que heredan habilidades, características y estadísticas | H9 |
| Relaciones entre agentes | H10 |
| Rol de cada personaje para la sociedad | H7 |
| Conversaciones diplomáticas, comercio | H13, H18 |
| Guerra y enfrentamientos por territorios | H8 (territorio), H14 |
| Sociedad violenta o pacífica según sus miembros | H9, H10, H14 |
| Mundo vivo que crece y avanza | H3, H6, H16, H17 |
| El jugador participa, se asocia o se integra en un clan | H15 |
| Aprovechar el volumen de animaciones | H0, H14, H15, H19 |
| El huerto como inicio de la agricultura y el asentamiento | H3, H8, H17 (era 2) |
| Avanzar hasta la era espacial | H17, H18 |
| El juego guarda el estado | H4 (y cada hito) |
| Simulación compleja | H5 a H18, H16, H20 |
| Reutilizable para jugador y NPC | Principio 1, H0, H1 |
| Pulir cada punto del juego | H19, H20, y la revisión al cerrar cada hito |
| Fase de cámaras y calidad gráfica tras la interacción | H19 |
| Entorno controlado con interactivos ampliables con la lógica base | H3, H17 |

## 6. Carriles de trabajo y orden

Para avanzar a la vez sin pisarse (una sesión o un agente por carril, cada uno con su rama y su PR):

| Carril | Contenido | Hitos |
|---|---|---|
| **A · Animación y personaje** | Árboles de animación, manifiesto de sets, sets nuevos, reacciones, muertes, niños y mayores | H0, partes de H9 y H14 |
| **B · Gameplay** | Habilidades, objetos, recursos, interactivos, estados, construcción | H1, H2, H3, H8 |
| **C · Mundo y navegación** | Navegación, percepción, regiones, ecología, exploración | H3, H5, H12, H16 |
| **D · Simulación social** | Necesidades, cerebro, roles, demografía, relaciones, tecnología | H6, H7, H9, H10, H11 |
| **E · Diplomacia y conflicto** | Clanes, comercio, guerra, combate | H13, H14, H18 |
| **F · Jugador y herramientas** | Interfaz, misiones, inspectores, editor de datos, QA de simulación | H15, H20 |
| **G · Calidad** | Cámaras, rendimiento, gráficos | H19 |

**Ruta crítica:** H0 → H1 → H2 → H3 → H4 → H5 → H6 → H7 → H8 → H9 → H10 → H11 → H13 → H14 → H15.
**En paralelo desde el principio:** H16 (se mide y se corrige en cada hito), H17 (los paquetes de era crecen con los hitos) y H20 (cada hito añade sus pruebas y sus herramientas).
**Regla de oro de integración:** antes de empezar cualquier carril se comprueba el estado de las ramas y de los PR abiertos; el que llega segundo fusiona `master` en su rama y resuelve, no duplica.

## 7. Animaciones y modelos que habrá que pedir

Con los packs actuales (locomoción, granja, heridos, magia, espada y escudo, arco, gestos) cubrimos las primeras eras y una parte del combate. Lo que falta, por orden de uso:

1. **Recoger del suelo y dejar** (*pick up*, *put down*), **recoger y dejar una caja**: para H2.
2. **Cavar en bucle**, **agacharse a recolectar** y **cortar con hacha en bucle** (*chopping*), **picar roca** (*mining*): para H2 y H3.
3. **Construcción**: martillear, levantar vigas, cargar tablones, subir una escalera: para H8.
4. **Levantarse del suelo**, de frente y de espaldas: para el ragdoll en H14.
5. **Niños y mayores**: andar, correr, idle y gestos con otra proporción o con retargeting y escala: para H9.
6. **Conversación**: hablar, escuchar, discutir, saludar, ceder, amenazar; **sentarse**, **dormir**, **comer**, **beber**: para H6, H10 y H13.
7. **Parto, enfermedad, curar, orar o ritual**: para H9 y H10.
8. **Combate extra**: lanza, hacha, mazo, lanzamiento, esquiva, ataque montado: para H14.
9. **Era industrial y posterior**: conducir, accionar palancas, trabajar en una máquina, teclear; **era espacial**: movimiento con traje, gravedad baja y gravedad cero.

Y los **modelos**: herramientas, semillas y plantas, regadera, pala, cestas, objetos de oficio, edificios por fases, ropa por era, animales, armas, vehículos. El motor puede generar versiones sencillas, pero la calidad final pide arte.

## 8. Riesgos transversales

- **Alcance.** Esto es una serie de años de trabajo de equipo, no de semanas. Por eso cada hito es una demo jugable y el plan se revisa al terminar cada fase: se sube, se baja o se descarta sin perder lo hecho.
- **Determinismo entre plataformas.** Los números en coma flotante difieren entre Windows y Linux y entre GPU. La simulación debe usar un núcleo numérico controlado y los tests de datos, con tolerancia, como ya hace el QA actual.
- **Rendimiento a escala.** Es el riesgo que más puede forzar cambios de arquitectura: medir desde H5 y fijar presupuestos por sistema.
- **Equilibrio.** Los sistemas emergentes se rompen fácil: barridos de semillas con métricas objetivo, desde el primer hito de cada sistema.
- **Contenido y arte.** Eras y personajes dependen de modelos y animaciones que no se generan solos: se piden por adelantado (apartado 7).
- **Coordinación entre agentes.** El mayor riesgo práctico: comprobar ramas y PR antes de empezar, documentar los formatos compartidos (manifiestos, esquemas de guardado) y fusionar a menudo.
- **Licencias.** Los packs de animación no se commitean; el repositorio solo lleva datos y sus manifiestos.

## 9. Primeros pasos

1. **Fusionar el huerto con el sistema de habilidades** (H1 y la primera parte de H3) tras traer `master`; revisar y cerrar lo que quede.
2. **Acordar con el agente de árboles de animación el formato del manifiesto de sets** y registrar los packs nuevos como datos (H0).
3. **Definir el contrato de actor** y el formato de guardado v1 con sus migraciones, antes de que crezcan los datos (H0 y H4).
4. **Objetos, inventario y recursos** (H2), con los montajes que falten pedidos al artista.
5. **Medir** el coste de un agente completo (animación, IK, física) para fijar los presupuestos de H6 y H16.
6. Cada hito se abre con su rama, su documento corto de diseño en `docs/` y sus criterios de aceptación copiados de aquí, y se cierra con su demo, sus tests y el cambio de estado de este documento.

## 10. Estado de los hitos

| Hito | Estado |
|---|---|
| H0 · Consolidación y contrato de actor | **Hecho** (rama `feature/actor-h0`): sets, interacciones, grafo de estados y línea de tiempo fusionados; manifiesto de datos con los siete packs (`AnimManifest`); **contrato de actor** (`Actor`, `ActorWorld`, `OrderSource`) con tests y coste medido (`docs/COSTE_ACTOR.md`) |
| H1 · Núcleo de habilidades (GAS) | En curso (`feature/huerto-gas`) |
| H2 · Objetos, inventario y recursos | **En curso** (núcleo hecho, [docs/OBJETOS.md](OBJETOS.md)): objetos, inventario con peso y apilado, nodos con regeneración, recetas, mochila por actor guardada con la partida y `actor bag`. Nodos de recurso como entidades con `Chop`, `Mine` y `Gather` y `resources build`. Falta: montajes propios de recolección, agarre visible, estaciones en la escena y datos en fichero |
| H3 · Mundo interactivo y ecología | **En curso**: huerto, nodos de recurso que se regeneran (bosque probado en simulación acelerada), la primera tribu ([TRIBU.md](TRIBU.md)) y los interactivos con estado persistente fogata, pozo y puerta ([INTERACTIVOS.md](INTERACTIVOS.md)). Falta: estaciones y clima sobre las plantas, animales con ecología básica, colisión de la puerta y panel de estados |
| H4 · Persistencia v1 | **Hecho** (`GB/Save`, [docs/GUARDADO.md](GUARDADO.md)): formato `.gbsave` versionado con migraciones y partidas antiguas en `tests/golden/saves/`, guardado diferencial sobre la semilla, escritura atómica con copia, autoguardado, repetición determinista y comandos del editor. Queda para otros hitos: inventarios (H2), enganchar el `Actor` (H0) y la edición de terreno |
| H5 a H20 | Sin empezar |

> Este documento se actualiza al cerrar cada hito: estado, decisiones tomadas y lo que se haya movido de sitio.
