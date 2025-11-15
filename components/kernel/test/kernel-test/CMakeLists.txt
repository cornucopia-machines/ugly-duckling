idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS "." "../../src"
                    REQUIRES kernel catch2 bblanchon__arduinojson
                    WHOLE_ARCHIVE)
