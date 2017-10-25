
#include "warp_mesh.h"

// Warper Class
Deform_Volume::Deform_Volume() {
  ;
}

Deform_Volume::Deform_Volume(void * data, int image_dims) {

  image_data = (unsigned char*)data;
  dims = image_dims;
  find_mesh_bounds();

  deformation_info = "";
}

long Deform_Volume::get_array_index(int x, int y, int z) {
  /* Image data is represented (to humans) as 3D voxel data e.g. array[x][y][z]. To CGAL,
  it is just a one dimensional array which contains all of the voxels.

  Return the one dimensinoal array location corresponding to a
  particular voxel in the image.
  */

  long idx;
  idx = (x * dims + y) * dims + z;

  if (idx >= dims*dims*dims) {
    //std::cout << "Array index too big!" << std::endl;
    return -1;
  }

  if (x < 0 || y < 0 || z < 0) {
    //std:cout << "Negative value passed!" << std::endl;
    return -1;
  }

  return idx;

}


void Deform_Volume::dilate_layer(int layer_index, int n_pixels) {
  /* Expands one a particular layer/tissue by one voxel on all sides
  layer_index: the layer/tissue index to be dilated
  n_pixels: how many pixels/voxels to dilate the layer by
  */
  cout << "Dilating layer " + to_string(layer_index) + " by " + to_string(n_pixels) + " pixels" <<endl;

  unsigned char layer_index_char = (unsigned char)layer_index;

  long n_elements = dims * dims * dims;
  long vector_element, i;
  vector<long> neighbours;
  unordered_set<long> elements_to_change;

  deformation_info += "_d." + to_string(layer_index) + "." + to_string(n_pixels);

  // Loop for each level of pixel dilation
  while (n_pixels--) {
    for (i = 0; i <  n_elements; i++) {
      if (image_data[i] == layer_index_char) {
        // Get the neighbouring elements
        neighbours = neighbouring_elements(i);

        // Add all neighbours to the list of elements to change
        while (!neighbours.empty()) {

          vector_element = neighbours.back();
          neighbours.pop_back();
          elements_to_change.insert(vector_element);

        }
      }
    }

    // Do the dilation
    // Go through the elements and change the layer type
    unordered_set<long>::iterator itr;
    for (itr = elements_to_change.begin(); itr != elements_to_change.end(); itr++) {
      image_data[*itr] = layer_index_char;
    }

  }

}

vector<long> Deform_Volume::neighbouring_elements (long voxel_index) {
  /* Calculate the indexes of elements neighbouring a partiuclar voxel in 3D array
  */

  int x,y,z; //Indexes of voxel

  z = voxel_index % dims;
  y = ( ( voxel_index - z ) / dims ) % dims;
  x = ( ( voxel_index - z) - y * dims) / (dims*dims);

  vector<long> neighbours;

  int i,j,k;
  long this_idx;

  for (i = x - 1; i <= x + 1; i++) {
    for (j = y - 1; j <= y + 1; j++) {
      for (k = z - 1; k <= z + 1; k++) {

        this_idx = get_array_index(i, j, k);

        // Check that voxel is within bounds [0 NDIMS] and that it is not the original voxel
        if (i >= 0 && i < dims &&  j >= 0 && j < dims && k >= 0 && k < dims &&
          this_idx != voxel_index) {
            neighbours.push_back(this_idx);
          }
        }
      }
    }

    return neighbours;

  }

  bool Deform_Volume::check_valid_points() {
    // Check that points are avalid and that new point will be within the the bounds of the array

    if (point_to_move == anchor) {
      cout << "Point to move cannot be the same as the anchor point" << endl;
      return false;
    }

    bool too_big = (point_to_move + distance_to_move) > dims;
    bool too_small = (point_to_move - distance_to_move) < 0;

    if (too_big || too_small) {
      cout << " Distance to move too great, new point outside bounds of array" << endl;
      return false;
    }

    return true;

  }


  void Deform_Volume::stretch_array(vector<int> stretch) {
    /* Stretch the image
    vector<int> stretch: 9 element vector (3 for x, 3 for y, 3 for z) that gives the paremetners
    of the stretch to be performed. For each dimension:
      1st element - point at which stretch starts
      2nd element - distance this point will be moved
      3rd element - anchor point. The stretching distance for each elment is relative
      to its distance from this point.

      Only points between the move point and the anchor point are stretched.

    */
    // Print stretch parameters
    cout << "Stretching with parameters:" << endl;
    int vec_i;

    for( vec_i = 0; vec_i < stretch.size(); ++vec_i) {
      cout << stretch[vec_i] << " ";
    }
    cout << endl;

    int this_idx, idx_to_copy_from;
    int new_x, new_y, new_z;

    // Create stretch objects in each dimension
    Stretch_Info x(stretch[0],stretch[1],stretch[2], dims);
    Stretch_Info y(stretch[3],stretch[4],stretch[5], dims);
    Stretch_Info z(stretch[6],stretch[7],stretch[8], dims);

    // Loop over each element that is affected by the stretch
    int i, j, k;
    for (i = x.start_iterate; i != x.end_iterate; i += x.step) {
      for (j = y.start_iterate; j != y.end_iterate; j += y.step) {
        for (k = z.start_iterate; k != z.end_iterate; k += z.step) {


          this_idx = get_array_index(i,j,k);

          // Calculate where to copy from
          new_x = x.idx_to_copy_from(i);
          new_y = y.idx_to_copy_from(j);
          new_z = z.idx_to_copy_from(k);
          idx_to_copy_from = get_array_index(new_x, new_y, new_z);

          // Copy point over
          image_data[this_idx] = image_data[idx_to_copy_from];

        }
      }
    }

    deformation_info += "_s";
    deformation_info += x.stretch_description() + ".";
    deformation_info += y.stretch_description() + ".";
    deformation_info += z.stretch_description();



  }
  void Deform_Volume::modify_image() {

    std::cout << std::endl << "MODIFYING IMAGE DATA" << endl;

    string stretch_info = "";

    srand(time(NULL));

    // Do at least one deformation
    random_stretch();

    // 50% chance of cotinuing
    while (rand() % 2) {
      find_mesh_bounds(); //Update edges of object

      //50% chance of stretch
      if (rand() %2) {
      random_stretch();

    }
    else {
    // 1 in 5 % chance of dilation
    if ((rand() %5) < 1) {
      random_dilate();
    }
  }
}
  }

  void Deform_Volume::random_stretch() {

    int max_stretch = 25;

    vector<int> stretch;

    //TODO: point_to_move, distance and anchor are class variables, is this needed anymore?
    //x data
    point_to_move = random_stretch_point(xmin, xmax);
    anchor = random_anchor_point(xmin, xmax);
    distance_to_move = rand() % (min (max_stretch, min(point_to_move, dims - point_to_move)));

    stretch.push_back(point_to_move);
    stretch.push_back(distance_to_move);
    stretch.push_back(anchor);

    //y data
    point_to_move = random_stretch_point(ymin, ymax);
    anchor = random_anchor_point(ymin, ymax);
    distance_to_move = rand() % (min (max_stretch, min(point_to_move, dims - point_to_move)));

    stretch.push_back(point_to_move);
    stretch.push_back(distance_to_move);
    stretch.push_back(anchor);

    //z data
    point_to_move = random_stretch_point(zmin, zmax);
    anchor = random_anchor_point(zmin, zmax);
    distance_to_move = rand() % (min (max_stretch, min(point_to_move, dims - point_to_move)));

    stretch.push_back(point_to_move);
    stretch.push_back(distance_to_move);
    stretch.push_back(anchor);

    // Randomly turn on or off stretchig in each dimension
    //TODO: this can be better!
    if (rand() % 2) {
      stretch[0] = -1; //x
    }

    if (rand() % 2) {
      stretch[3] = -1; //y
    }

    if (rand() % 2) {
      stretch[6] = -1; //z
    }

    stretch_array(stretch);
  }


  int Deform_Volume::random_stretch_point(int idx_min, int idx_max) {
    // Return a stretch point that is < idx_min or > idx_max

    // > idx_max
    int upper_rand = idx_max + ( rand() % (dims - idx_max));

    // If the minimum value is 0, we can't stretch further in this direciton so
    // only use the upper value
    // Even if min > 0, we want to pick at random between a value < idx_min and > idx_max
    int even_odd = rand() %2;
    if ( (idx_min == 0) || even_odd ) {
      return upper_rand;
    }

    //  return value between 1 and idx_min
    int lower_rand = 1 + rand() % idx_min;
    return lower_rand;

  }

  int Deform_Volume::random_anchor_point(int idx_min, int idx_max) {
    // Want to pick an anchor point  'within' the object
    // i.e. >idx_min and < idx_max
    return idx_min + rand() % ( idx_max - idx_min );

  }

  void Deform_Volume::random_dilate() {
    // Dilate a random layer by a random amount (either  1 or 2 pixels)

    int layer, dilate_amount;
    int max_dilate = 1;

    // Get list of layer indices and select one at random
    get_layers();
    int random_index = rand() % layers.size();
    layer = layers[random_index];

    dilate_amount  =1 + rand() % max_dilate;

    dilate_layer(layer, dilate_amount);

  }


  void Deform_Volume::get_layers() {
    // Placeholder for function to loop through array and return a vector of all the unique values
    // TODO: implment properly

    for (int i = 1; i <= 7; i++) {
      layers.push_back(i);
    }

  }


  void Deform_Volume::find_mesh_bounds() {
    // Find the first/last non 0 element along each dimensional
    // by looping over all elments

    // reset bounds
    xmax = INT_MIN, ymax = INT_MIN, zmax = INT_MIN;
    xmin = INT_MAX, ymin = INT_MAX, zmin = INT_MAX;

    int i,j,k;
    long idx;

    for (i = 0; i < dims; i++) {
      for (j = 0; j < dims; j++) {
        for (k = 0; k < dims; k++) {

          idx = get_array_index(i, j, k);
          // Is this element non 0 i.e. not background
          // if so check if the min/max values should be updated
          if (image_data[idx] != 0) {

            if (i > xmax) xmax = i;
            if (i < xmin) xmin = i;

            if (j > ymax) ymax = j;
            if (j < ymin) ymin = j;

            if (k > zmax) zmax = k;
            if (k < zmin) zmin = k;

          }
        }
      }
    }

    printf("Bounds of image are X: %d %d  Y: %d %d  Z: %d %d\n", xmin, xmax,
    ymin, ymax, zmin, zmax);
  }

  /*
  ########################################################
  End of Warper class
  ############################################################
  */
