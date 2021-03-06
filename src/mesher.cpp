// include/defines used across all  statements
#include "CGAL_include.h"

#include "input_parameters.h"
#include "Sizing_fields.h"
#include "mesh_operations.h"
#include "save_dgf.h"
#include "deform_volume.h"

#include "write_c3t3_to_vtk_xml_file.h"

// To avoid verbose function and named parameters call
using namespace CGAL::parameters;
using namespace std;

/** Print correct command line usage.
 **/
void printusage(void)
{
  printf("Usage:  ./mesher -i INPUT_INR -e INPUT_ELECTRODES -p INPUT_PARAMETERS");
  printf("        -i input image file\n");
  printf("        -e electrode position file\n");
  printf("        -p parameter file\n");
  printf("        -o output mesh name (default = new_mesh)\n");
  printf("        -d output directory (default = output/)\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{

  int opt;
  // Input file locations (required as arguments)
  char *path_image, *path_electrode, *path_parameter;
  bool image_path_set = false, elec_path_set = false, param_path_set = false;
  // Default values, can be changed with command line arguments
  string output_dir = "./output/";
  string input_mesh_name = "new_mesh";

  //Used if deforming mesh, to modify the output name
  string output_mesh_name, output_base_file;

  // store mesh quality metrics
  vector<double> mesh_quality_metrics(3);

  // Process input parameters
  while ((opt = getopt(argc, argv, "i:e:p:o:d:")) != -1)
  {
    switch (opt)
    {
    case 'i':
      path_image = optarg;
      image_path_set = true;
      break;
    case 'e':
      path_electrode = optarg;
      elec_path_set = true;
      break;
    case 'p':
      path_parameter = optarg;
      param_path_set = true;
      break;
    case 'd':
      output_dir = optarg;
      break;
    case 'o':
      input_mesh_name = optarg;
      break;
    }
  }

  if (!image_path_set || !elec_path_set || !param_path_set)
  {
    printusage();
  }

  // Output file names for sanity check
  cout << "Input file: " << path_image << "\n";
  cout << "Electrode file: " << path_electrode << "\n";
  cout << "Parameter file: " << path_parameter << "\n";
  cout << "Output directory: " << output_dir << "\n";
  cout << "Output mesh name: " << input_mesh_name << "\n\n";

  // Read input file with parameters
  map<string, FT> options = read_params_from_file(path_parameter);

  // Loads image
  CGAL::Image_3 image;
  cout << "Reading the Image file... " << endl;

  image.read(path_image);
  cout << "Dimensions of image: " << image.xdim() << " | " << image.ydim() << " | " << image.zdim() << endl;

  options["vx"] = image.vx();
  options["vy"] = image.vy();
  options["vz"] = image.vz();

  options["xdim"] = image.xdim();
  options["ydim"] = image.ydim();
  options["zdim"] = image.zdim();

  // Set to default value
  output_mesh_name = input_mesh_name;

  // Do the stretch in input file
  if (options["do_deformation"])
  {
    cout << "Deforming mesh." << endl;

    Deform_Volume warper(&image, options);
    warper.modify_image();

    // Append mesh_name with random integer. 
    long long random_int = rand() * rand();
    output_mesh_name = input_mesh_name + to_string(random_int);
    cout << "New mesh name: " << output_mesh_name << endl;

    string deformation_file = output_dir + output_mesh_name + ".deformations";
    save_deformation_info(warper.deformation_info, deformation_file);

  }

  Mesh_domain domain(image);

  //Define Sizing field
  // vx, vy and vz are the size of each voxel
  // xdim, ydim and zdim are the number of voxels along each axis
  Point centre(image.vx() * image.xdim() / 2,
               image.vy() * image.ydim() / 2,
               image.vz() * image.zdim() / 2);

  Sizing_field sizing_field(centre, path_electrode, options); //This is basic and working now for both rat and human

  // Mesh criteria: faces and cells
  Mesh_criteria criteria(facet_angle = options["facet_angle_deg"], facet_size = sizing_field, facet_distance = options["facet_distance_mm"],
                         cell_radius_edge_ratio = options["cell_radius_edge_ratio"], cell_size = sizing_field);

  // Meshing

  cout << endl
       << "Creating initial mesh..." << flush;

  C3t3_EIT c3t3;

  c3t3 = CGAL::make_mesh_3<C3t3_EIT>(domain, criteria, CGAL::parameters::features(domain),
                                     CGAL::parameters::no_lloyd(),
                                     CGAL::parameters::no_odt(),
                                     CGAL::parameters::no_perturb(),
                                     CGAL::parameters::no_exude());

  cout << "number of tetra: " << c3t3.number_of_cells_in_complex() << endl;

  //Optimisation - this is the preferred order to run the optimisations in
  //according to CGAL documentation.

  if ((int(options["odt_opt"]) == 1) ||
      (int(options["lloyd_opt"]) == 1) ||
      (int(options["perturb_opt"]) == 1) ||
      (int(options["exude_opt"]) == 1))
  {
    optimise_mesh(c3t3, domain, options);
  }

  Point ground_node = c3t3.set_ground_node();

  if (options["move_electrodes"])
  {
    cout << endl
         << "Moving electrodes to closest facets: " << endl;

    int outer_tissue_index = options["outermost_tissue"];
    for (int i = 0; i < sizing_field.centres.size(); ++i)
    {
      sizing_field.centres[i] = c3t3.find_closest_element(sizing_field.centres[i], outer_tissue_index);
    }
  }

  // Put together parameters
  map<string, string> parameters;

  // Add all options to the parameter map

  for (auto elem : options)
  {
    // make the stream object - this makes nicer strings that to_string
    ostringstream currentVal;
    currentVal << elem.second;
    parameters[elem.first] = currentVal.str(); // save into the parameters map
  }

  parameters["ground.hsquared"] = string("1.5e-5");

  // Need to convert double to string before adding to parameter map
  // using ostringstream to do this
  ostringstream gndposx, gndposy, gndposz;
  gndposx << CGAL::to_double(ground_node.x()) / MM_TO_M;
  gndposy << CGAL::to_double(ground_node.y()) / MM_TO_M;
  gndposz << CGAL::to_double(ground_node.z()) / MM_TO_M;

  parameters["groundposition.x"] = gndposx.str();
  parameters["groundposition.y"] = gndposy.str();
  parameters["groundposition.z"] = gndposz.str();

  // Base filenames for electrode positions and parameters
  output_base_file = output_dir + output_mesh_name;

  // Output dgf file and electrode_positions
  save_as_dgf(c3t3, options, output_base_file);
  save_electrodes(sizing_field.centres, output_base_file);
  save_parameters(parameters, output_base_file, input_mesh_name);

  if (options["save_cell_centres"])
  {
    write_centres(c3t3, output_base_file);
  }

  if (int(options["save_nodes_tetra"]) == 1)
  {
    save_matlab(c3t3, options, output_base_file);
  }

  // Output the mesh for Paraview
  string vtk_file_path = output_base_file + ".vtu";

  if (int(options["save_vtk"]) == 1)
  {
    cout << "Writing vtu file: " << vtk_file_path << endl;
    int vtk_success = write_c3t3_to_vtk_xml_file(c3t3, vtk_file_path);
  }

  mesh_quality_metrics = check_mesh_quality(c3t3);

  // add this here again so its easier to see in terminal
  cout << "All done! Created mesh with " << c3t3.number_of_cells_in_complex() << " elements, and " << mesh_quality_metrics.at(2) << " average quality" << endl;

  return 0;
}
