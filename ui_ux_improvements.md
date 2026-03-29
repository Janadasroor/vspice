## Symbol Editor UI/UX & Feature Roadmap

*   **Live Rendered Preview:** Implement a dockable "Live Preview" pane that uses the core rendering engine to show exactly how the symbol will appear on the schematic canvas (including text eliding, color themes, and label positioning).
*   **Datasheet Tracing Overlay:** Add a "Reference Image" layer with adjustable opacity, allowing users to import datasheet screenshots as a background guide for high-fidelity tracing of complex footprints and symbols.
*   **Intelligent Pin Array Tool:** Introduce a specialized tool for generating pin arrays (SIP, DIP, Quad, BGA) with customizable pitch, naming patterns (e.g., 1..N, A..Z), and automatic electrical type assignment.
*   **Contextual Drawing Properties:** Modernize the toolbar to be contextual; selecting a Pin should immediately show its Name/Number/Type controls in a compact floating bar, reducing trips to the side panels.
*   **AI-Assisted Generating:** Enhance the existing Gemini integration to support "Symbol from Paste," allowing users to paste a pinout table from a datasheet and have AI automatically generate the entire symbol structure.
*   **Visual Health Markers (SRC):** Integrate Rule Checker results directly onto the canvas with subtle badges (e.g., "!" icons) for duplicate pin numbers or unanchored primitives.
