/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2018, assimp team



All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file  Main.cpp
 *  @brief main() function of assimp_cmd
 */

#include "Main.h"
#include <assimp/SceneCombiner.h>
#include <iostream>
#include <fstream>
#include "json.hpp"
using json = nlohmann::json;

const char* AICMD_MSG_ABOUT =
"------------------------------------------------------ \n"
"Open Asset Import Library (\"Assimp\", https://github.com/assimp/assimp) \n"
" -- Commandline toolchain --\n"
"------------------------------------------------------ \n\n"

"Version %i.%i %s%s%s%s%s(GIT commit %x)\n\n";

const char* AICMD_MSG_HELP =
"assimp <verb> <parameters>\n\n"
" verbs:\n"
" \tinfo       - Quick file stats\n"
" \tlistext    - List all known file extensions available for import\n"
" \tknowext    - Check whether a file extension is recognized by Assimp\n"
#ifndef ASSIMP_BUILD_NO_EXPORT
" \texport     - Export a file to one of the supported output formats\n"
" \tlistexport - List all supported export formats\n"
" \texportinfo - Show basic information on a specific export format\n"
#endif
" \textract    - Extract embedded texture images\n"
" \tdump       - Convert models to a binary or textual dump (ASSBIN/ASSXML)\n"
" \tcmpdump    - Compare dumps created using \'assimp dump <file> -s ...\'\n"
" \tversion    - Display Assimp version\n"
"\n Use \'assimp <verb> --help\' for detailed help on a command.\n"
;

/*extern*/ Assimp::Importer* globalImporter = NULL;

#ifndef ASSIMP_BUILD_NO_EXPORT
/*extern*/ Assimp::Exporter* globalExporter = NULL;
#endif

json scenario_json({
  { "assembly", {} },
  { "components", {} }
});

void AddToJson(aiNode* pcNode);
// ------------------------------------------------------------------------------
// Application entry point

int main(int argc, char* argv[]) {

  if (argc < 2) {
    printf("Invalid command\n");
    return 0;
  }

  //get the filenames
  std::string in_file = argv[1];
  std::string out_json = argv[2];

  //in file without file extension
  size_t lastindex = in_file.find_last_of(".");
  std::string in_file_noext = in_file.substr(0, lastindex);

  ImportData import_settings;
  import_settings.ppFlags |= aiProcess_FixInfacingNormals;
  import_settings.ppFlags |= aiProcess_FindDegenerates;
  import_settings.ppFlags |= aiProcess_FindInstances;
  import_settings.ppFlags |= aiProcess_Triangulate;
  import_settings.ppFlags |= aiProcess_ImproveCacheLocality;
  import_settings.ppFlags |= aiProcess_RemoveComponent;

  Importer importer;
  // Only keep triangles
  importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
  // throw away all this stuff, don't need it (and can mess up vertex identification, see: https://github.com/assimp/assimp/issues/407)
  importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS,
    aiComponent_NORMALS |
    aiComponent_TANGENTS_AND_BITANGENTS |
    aiComponent_COLORS |
    aiComponent_LIGHTS |
    aiComponent_CAMERAS
  );

  DefaultLogger::create("", Logger::VERBOSE, aiDefaultLogStream_STDERR);
  const aiScene* scene_o = importer.ReadFile(in_file, import_settings.ppFlags);
  std::cout << "Total components: " << scene_o->mNumMeshes << "\n";

  //write the mesh ply files for the scene
  for (int i = 0; i < scene_o->mNumMeshes; i++) {
    //create a new scene with one node
    aiScene* c_scene = new aiScene();
    c_scene->mMeshes = new aiMesh*[1];
    c_scene->mMeshes[0] = scene_o->mMeshes[i]; //exporting the ith mesh
    c_scene->mNumMeshes = 1;
    c_scene->mMaterials = scene_o->mMaterials;
    c_scene->mNumMaterials = scene_o->mNumMaterials;

    //create the root node
    c_scene->mRootNode = new aiNode(scene_o->mRootNode->mName.C_Str());
    c_scene->mRootNode->mMeshes = new unsigned int[1];
    c_scene->mRootNode->mMeshes[0] = 0; //index to the mesh array in scene
    c_scene->mRootNode->mNumMeshes = 1;

    //use exporter on the scene
    Exporter exporter;
    auto o_filename = in_file_noext + "_part_" + std::to_string(i) + ".ply";
    exporter.Export(c_scene, "ply", o_filename, !aiProcess_PreTransformVertices);

    //write component filename and id to json
    json component = {
      { "file", o_filename },
      { "id", i }
    };
    scenario_json["components"].push_back(component);
  }

  //write out the components (mesh and applied transformation)
  AddToJson(scene_o->mRootNode);
  std::ofstream scenario_file(out_json, std::ios::out | std::ios::binary);
  scenario_file << std::setprecision(std::numeric_limits<double>::max_digits10) << scenario_json;

  return 1;
}

void AddToJson(aiNode* pcNode)
{
  //if the current node has a parent, compose the transformation
  if (pcNode->mParent) {
    pcNode->mTransformation = pcNode->mParent->mTransformation*pcNode->mTransformation;
  }

  //if the node has some components, write out to the json
  if (pcNode->mNumMeshes > 0) {
    for (int i = 0; i < pcNode->mNumMeshes; i++) {
      //the mesh idx in the scene
      auto idx = pcNode->mMeshes[i];

      //get the mesh id for idx-th mesh
      int mesh_id = scenario_json["components"][idx]["id"];

      //add to json
      json component = {
        { "component", mesh_id }
      };
      component["transform"] = std::vector<double>(16);
      for (int j = 0; j < 4; j++) {
        for (int k = 0; k < 4; k++) {
          component["transform"][(j * 4) + k] = pcNode->mTransformation[j][k];
        }
      }
      scenario_json["assembly"].push_back(component);
    }
  }

  //recursively process the children
  for (unsigned int i = 0; i < pcNode->mNumChildren; ++i) {
    AddToJson(pcNode->mChildren[i]);
  }
}

// ------------------------------------------------------------------------------
void SetLogStreams(const ImportData& imp)
{
	printf("\nAttaching log stream   ...           OK\n");

	unsigned int flags = 0;
	if (imp.logFile.length()) {
		flags |= aiDefaultLogStream_FILE;
	}
	if (imp.showLog) {
		flags |= aiDefaultLogStream_STDERR;
	}
	DefaultLogger::create(imp.logFile.c_str(),imp.verbose ? Logger::VERBOSE : Logger::NORMAL,flags);
}


// ------------------------------------------------------------------------------
void FreeLogStreams()
{
	DefaultLogger::kill();
}


// ------------------------------------------------------------------------------
void PrintHorBar()
{
	printf("-----------------------------------------------------------------\n");
}


// ------------------------------------------------------------------------------
// Import a specific file
const aiScene* ImportModel(
	const ImportData& imp,
	const std::string& path)
{
	// Attach log streams
	if (imp.log) {
		SetLogStreams(imp);
	}
	printf("Launching asset import ...           OK\n");

	// Now validate this flag combination
	if(!globalImporter->ValidateFlags(imp.ppFlags)) {
		printf("ERROR: Unsupported post-processing flags \n");
		return NULL;
	}
	printf("Validating postprocessing flags ...  OK\n");
	if (imp.showLog) {
		PrintHorBar();
	}


	// do the actual import, measure time
	const clock_t first = clock();
	const aiScene* scene = globalImporter->ReadFile(path,imp.ppFlags);

	if (imp.showLog) {
		PrintHorBar();
	}
	if (!scene) {
		printf("ERROR: Failed to load file: %s\n", globalImporter->GetErrorString());
		return NULL;
	}

	const clock_t second = ::clock();
	const double seconds = static_cast<double>(second-first) / CLOCKS_PER_SEC;

	printf("Importing file ...                   OK \n   import took approx. %.5f seconds\n"
		"\n",seconds);

	if (imp.log) {
		FreeLogStreams();
	}
	return scene;
}

#ifndef ASSIMP_BUILD_NO_EXPORT
// ------------------------------------------------------------------------------
bool ExportModel(const aiScene* pOut,
	const ImportData& imp,
	const std::string& path,
	const char* pID)
{
	// Attach log streams
	if (imp.log) {
		SetLogStreams(imp);
	}
	printf("Launching asset export ...           OK\n");

	if (imp.showLog) {
		PrintHorBar();
	}

	// do the actual export, measure time
	const clock_t first = clock();
	const aiReturn res = globalExporter->Export(pOut,pID,path);

	if (imp.showLog) {
		PrintHorBar();
	}
	if (res != AI_SUCCESS) {
		printf("Failed to write file\n");
		printf("ERROR: %s\n", globalExporter->GetErrorString());
		return false;
	}

	const clock_t second = ::clock();
	const double seconds = static_cast<double>(second-first) / CLOCKS_PER_SEC;

	printf("Exporting file ...                   OK \n   export took approx. %.5f seconds\n"
		"\n",seconds);

	if (imp.log) {
		FreeLogStreams();
	}

	return true;
}
#endif

// ------------------------------------------------------------------------------
// Process standard arguments
int ProcessStandardArguments(
	ImportData& fill,
	const char* const * params,
	unsigned int num)
{
	// -ptv    --pretransform-vertices
	// -gsn    --gen-smooth-normals
	// -gn     --gen-normals
	// -cts    --calc-tangent-space
	// -jiv    --join-identical-vertices
	// -rrm    --remove-redundant-materials
	// -fd     --find-degenerates
	// -slm    --split-large-meshes
	// -lbw    --limit-bone-weights
	// -vds    --validate-data-structure
	// -icl    --improve-cache-locality
	// -sbpt   --sort-by-ptype
	// -lh     --convert-to-lh
	// -fuv    --flip-uv
	// -fwo    --flip-winding-order
	// -tuv    --transform-uv-coords
	// -guv    --gen-uvcoords
	// -fid    --find-invalid-data
	// -fixn   --fix normals
	// -tri    --triangulate
	// -fi     --find-instances
	// -og     --optimize-graph
	// -om     --optimize-meshes
	// -db     --debone
	// -sbc    --split-by-bone-count
	//
	// -c<file> --config-file=<file>

	for (unsigned int i = 0; i < num;++i)
	{
		if (! strcmp(params[i], "-ptv") || ! strcmp(params[i], "--pretransform-vertices")) {
			fill.ppFlags |= aiProcess_PreTransformVertices;
		}
		else if (! strcmp(params[i], "-gsn") || ! strcmp(params[i], "--gen-smooth-normals")) {
			fill.ppFlags |= aiProcess_GenSmoothNormals;
		}
		else if (! strcmp(params[i], "-gn") || ! strcmp(params[i], "--gen-normals")) {
			fill.ppFlags |= aiProcess_GenNormals;
		}
		else if (! strcmp(params[i], "-jiv") || ! strcmp(params[i], "--join-identical-vertices")) {
			fill.ppFlags |= aiProcess_JoinIdenticalVertices;
		}
		else if (! strcmp(params[i], "-rrm") || ! strcmp(params[i], "--remove-redundant-materials")) {
			fill.ppFlags |= aiProcess_RemoveRedundantMaterials;
		}
		else if (! strcmp(params[i], "-fd") || ! strcmp(params[i], "--find-degenerates")) {
			fill.ppFlags |= aiProcess_FindDegenerates;
		}
		else if (! strcmp(params[i], "-slm") || ! strcmp(params[i], "--split-large-meshes")) {
			fill.ppFlags |= aiProcess_SplitLargeMeshes;
		}
		else if (! strcmp(params[i], "-lbw") || ! strcmp(params[i], "--limit-bone-weights")) {
			fill.ppFlags |= aiProcess_LimitBoneWeights;
		}
		else if (! strcmp(params[i], "-vds") || ! strcmp(params[i], "--validate-data-structure")) {
			fill.ppFlags |= aiProcess_ValidateDataStructure;
		}
		else if (! strcmp(params[i], "-icl") || ! strcmp(params[i], "--improve-cache-locality")) {
			fill.ppFlags |= aiProcess_ImproveCacheLocality;
		}
		else if (! strcmp(params[i], "-sbpt") || ! strcmp(params[i], "--sort-by-ptype")) {
			fill.ppFlags |= aiProcess_SortByPType;
		}
		else if (! strcmp(params[i], "-lh") || ! strcmp(params[i], "--left-handed")) {
			fill.ppFlags |= aiProcess_ConvertToLeftHanded;
		}
		else if (! strcmp(params[i], "-fuv") || ! strcmp(params[i], "--flip-uv")) {
			fill.ppFlags |= aiProcess_FlipUVs;
		}
		else if (! strcmp(params[i], "-fwo") || ! strcmp(params[i], "--flip-winding-order")) {
			fill.ppFlags |= aiProcess_FlipWindingOrder;
		}
		else if (! strcmp(params[i], "-tuv") || ! strcmp(params[i], "--transform-uv-coords")) {
			fill.ppFlags |= aiProcess_TransformUVCoords;
		}
		else if (! strcmp(params[i], "-guv") || ! strcmp(params[i], "--gen-uvcoords")) {
			fill.ppFlags |= aiProcess_GenUVCoords;
		}
		else if (! strcmp(params[i], "-fid") || ! strcmp(params[i], "--find-invalid-data")) {
			fill.ppFlags |= aiProcess_FindInvalidData;
		}
		else if (! strcmp(params[i], "-fixn") || ! strcmp(params[i], "--fix-normals")) {
			fill.ppFlags |= aiProcess_FixInfacingNormals;
		}
		else if (! strcmp(params[i], "-tri") || ! strcmp(params[i], "--triangulate")) {
			fill.ppFlags |= aiProcess_Triangulate;
		}
		else if (! strcmp(params[i], "-cts") || ! strcmp(params[i], "--calc-tangent-space")) {
			fill.ppFlags |= aiProcess_CalcTangentSpace;
		}
		else if (! strcmp(params[i], "-fi") || ! strcmp(params[i], "--find-instances")) {
			fill.ppFlags |= aiProcess_FindInstances;
		}
		else if (! strcmp(params[i], "-og") || ! strcmp(params[i], "--optimize-graph")) {
			fill.ppFlags |= aiProcess_OptimizeGraph;
		}
		else if (! strcmp(params[i], "-om") || ! strcmp(params[i], "--optimize-meshes")) {
			fill.ppFlags |= aiProcess_OptimizeMeshes;
		}
		else if (! strcmp(params[i], "-db") || ! strcmp(params[i], "--debone")) {
			fill.ppFlags |= aiProcess_Debone;
		}
		else if (! strcmp(params[i], "-sbc") || ! strcmp(params[i], "--split-by-bone-count")) {
			fill.ppFlags |= aiProcess_SplitByBoneCount;
		}


		else if (! strncmp(params[i], "-c",2) || ! strncmp(params[i], "--config=",9)) {

			const unsigned int ofs = (params[i][1] == '-' ? 9 : 2);

			// use default configurations
			if (! strncmp(params[i]+ofs,"full",4)) {
				fill.ppFlags |= aiProcessPreset_TargetRealtime_MaxQuality;
			}
			else if (! strncmp(params[i]+ofs,"default",7)) {
				fill.ppFlags |= aiProcessPreset_TargetRealtime_Quality;
			}
			else if (! strncmp(params[i]+ofs,"fast",4)) {
				fill.ppFlags |= aiProcessPreset_TargetRealtime_Fast;
			}
		}
		else if (! strcmp(params[i], "-l") || ! strcmp(params[i], "--show-log")) {
			fill.showLog = true;
		}
		else if (! strcmp(params[i], "-v") || ! strcmp(params[i], "--verbose")) {
			fill.verbose = true;
		}
		else if (! strncmp(params[i], "--log-out=",10) || ! strncmp(params[i], "-lo",3)) {
			fill.logFile = std::string(params[i]+(params[i][1] == '-' ? 10 : 3));
			if (!fill.logFile.length()) {
				fill.logFile = "assimp-log.txt";
			}
		}
	}

	if (fill.logFile.length() || fill.showLog || fill.verbose) {
		fill.log = true;
	}

	return 0;
}

// ------------------------------------------------------------------------------
int Assimp_TestBatchLoad (
	const char* const* params,
	unsigned int num)
{
	for(unsigned int i = 0; i < num; ++i) {
		globalImporter->ReadFile(params[i],aiProcessPreset_TargetRealtime_MaxQuality);
		// we're totally silent. scene destructs automatically.
	}
	return 0;
}
