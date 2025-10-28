// system/exporter_base/exporterBase.h
#pragma once
#include "mesh.h"
#include <string>

/**
 * @class FemExporter
 * @brief FEM output file's main exporter (Facade)
 *
 * Provides a single static method to save a Mesh object to a file.
 * It is responsible for opening/closing the file and dispatching tasks
 * to specialized sub-exporters based on the data structure.
 */
class FemExporter {
public:
    /**
     * @brief Saves the Mesh object to a specified output file.
     * @param filepath The path to the file to be saved.
     * @param mesh [in] The Mesh object containing the data to be exported.
     * @return true if saving is successful, false if the file cannot be opened or a critical error occurs.
     */
    static bool save(const std::string& filepath, const Mesh& mesh);
};
