// system/exporter_base/exporterBase.h
#pragma once
#include "DataContext.h"
#include <string>

/**
 * @class FemExporter
 * @brief FEM output file's main exporter (Facade)
 *
 * Provides a single static method to save mesh data from DataContext to a file.
 * It is responsible for opening/closing the file and dispatching tasks
 * to specialized sub-exporters based on the ECS component structure.
 */
class FemExporter {
public:
    /**
     * @brief Saves the mesh data from DataContext's registry to a specified output file.
     * @param filepath The path to the file to be saved.
     * @param data_context [in] The DataContext object containing the registry with mesh data.
     * @return true if saving is successful, false if the file cannot be opened or a critical error occurs.
     */
    static bool save(const std::string& filepath, const DataContext& data_context);
};
