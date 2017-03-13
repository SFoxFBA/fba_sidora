<?php

/**
 * Checks that a CSV file has been uploaded successfully.
 *
 * return boolean true if a file has been uploaded successfully and it is of type csv
 * 	(i.e. it has the extension .csv); return false otherwise
 */
function _check_filetype_uploaded($filename) {
  $valid_CSV = true; // default value;
  if (strcasecmp(end(explode('.', $filename)), 'csv') !== 0) {
    $valid_CSV = false;
  }
  return $valid_CSV;
}

function _create_datastream(&$fedora_object, $dsid, $controlGroup = "X", $label = "New Datastream", $mimetype, $content, $from_file = false) {
  $new_datastream = $fedora_object->constructDatastream($dsid, $controlGroup);
  $new_datastream->label = $label;
  $new_datastream->mimetype = $mimetype;
  if ($from_file) {  // TODO can set a flag within the parameters for create_datastream, to indicate that it needs to set datastream content from a file rather than string (for CSV fix)
    $new_datastream->setContentFromFile($content);
  }
  else {
    $new_datastream->setContentFromString($content);
  }

  return $new_datastream;
}

/**
 * Checks a valid CSV function has been uploaded
 *
 * @param array $form
 *   standard form variable for a hook
 * @param array $form_state
 *   standard form state variable for a hook
 * 
 */
function resource_ingest_form_validate($form, &$form_state, $maxfiles = 20) {
  module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');
  global $user;

  $valid_CSV = array(); //default init value
//SFOX ripped this from sidora batch ingest code in ingest.inc
  unset($_SESSION['messages']);

  /* start of code added by NAB */
  if (isset($form['archivalRecordCreationDate']['#value'])) {
    date_default_timezone_set('Europe/London'); //SFOX added this to stop php error
    $date_str = sprintf("%s %s", date("Y-m-d"), date("H:i:s"));
    $form['archivalRecordCreationDate']['#value'] = $date_str;
    $form['#post']['archivalRecordCreationDate'] = $date_str; // This is the assignment that actually results in the date string being entered in the form.
    $form_state['values']['archivalRecordCreationDate'] = $date_str;
    $_POST['archivalRecordCreationDate'] = $date_str;
  }
  /* end of code added by NAB */

  validate_uploaded_files($form, &$form_state, $maxfiles);
  return;
}

function validate_uploaded_files($form, &$form_state, $maxfiles = 20, $mode = 'new') {
//Sidora uploads need to use a stored version of the session id
  $sessionid = $_SESSION['sidora_file_sessionid'];
  if (!$sessionid) {
    $sessionid = $form_state['values']['form_build_id'];
  }
  $files = $_SESSION['file_uploads'][$sessionid];

  if (!empty($files)) {
//Check if the filename already exists in this object
    $pid = $form['#parameters']['2'];
//this is copied and pasted code... PUT IN A FUNCTION!
    $result_object = islandora_object_load($pid);
    if ($result_object) {
      foreach ($result_object as $datastream) {
        if (preg_match('/^OBJ/', $datastream->id)) {
          $dslabel = $datastream->label;
          foreach ($files as $file) {
            $objfilename = $file->filename;
            if (strcmp($dslabel, $objfilename) === 0) {
              $foundmatchinglabels[$objfilename] = $objfilename;
            }
          }
        }
      }
    }
    if ($foundmatchinglabels) {
      $errormessage .= 'There is already a csv with filename(s): <br/>';
      foreach ($foundmatchinglabels as $foundmatchinglabel) {
        $errormessage .= $foundmatchinglabel . '<br/>';
      }
      $errormessage .= '<br/>';
    }
//Max files check
    if (count($files) > $maxfiles) {
      $errormessage .= 'A maximum of ' . $maxfiles;
      if ($maxfiles == 1) {
        $errormessage .= ' file ';
      }
      else {
        $errormessage .= ' files ';
      }

      if (strcmp($mode, 'new') === 0) {
        $errormessage .= 'can be uploaded per object<br/>';
      }
      else {
        $errormessage .= 'can be additionally uploaded to this object<br/>';
      }
    }
    else {
      $fileindex = 0;
      foreach ($files as $filetoprocess) {
        $filename[$fileindex] = $filetoprocess->filename;
        $filepath[$fileindex] = $filetoprocess->filepath;

        $valid_CSV[$fileindex] = _check_filetype_uploaded($filename[$fileindex]);
        if ($valid_CSV[$fileindex]) {
          // File is a CSV, can start to process it
          // Set files to form_state, to process when form is submitted.
          // $file = file_get_contents($_FILES['uploaded_file']['tmp_name']); //SFOX altered this
          $file = file_get_contents($filepath[$fileindex]);

          $form_state['values']['file'][$fileindex] = $file; //needed?
          // Processing First Row of CSV file (user defined headers)
          // Check the labels in first row for non-ascii characters
          $invalid_label_found = false;
          $userlabels = explode(",", strtok($file, "\n"));
          $num_labels = count($userlabels);
          for ($i = 0; $i < $num_labels; $i++) {
            $label = trim($userlabels[$i]);
            // Check terms. Set flag if invalid vocab term found
            $checked_result = mb_detect_encoding($label, 'ASCII', true);
            if ($checked_result == NULL) {
              $invalid_label_found[$fileindex] = true;
              $invalid_labels[$fileindex][$i] = $label;
            }
            else {
              $valid_labels[$fileindex][$i] = $label;
            }
          }
          $valid_label[$fileindex] = !($invalid_label_found[$fileindex]);
          // Processing Second Row of CSV files (vocab terms)
          // Examine the second row and check if each entry in this row is a DTC 
          // preferred vocab term
          $vocab_terms = trim_array_values(explode(",", strtok("\n")));
          // NB TODO replace the use of explode() with fgetcsv (in case cells have commas in them)

          $invalid_vocab_term_found = false;
          $num_vocab_terms = count($vocab_terms);
          for ($i = 0; $i < $num_vocab_terms; $i++) {
            $term = trim($vocab_terms[$i]);
            // Check terms. Set flag if invalid vocab term found
            $checked_result = _check_vocab_term(&$term);
            if ($checked_result == NULL) {
              $invalid_vocab_term_found[$fileindex] = true;
              $invalid_vocab_terms[$fileindex][$i] = $term;
            }
            else {
              $valid_vocab_terms[$fileindex][$i] = $term;
            }
          }
          $valid_CSV[$fileindex] = !($invalid_vocab_term_found[$fileindex]);

          //Process the third row
          $unit_terms = trim_array_values(explode(",", strtok("\n")));
          //$fileindex is repeated twice validly on the next line
          validate_csv_unit_taxon_line($unit_terms, $valid_CSV, $fileindex, $fileindex, $invalid_unit_terms, $invalid_taxon_terms, $invalid_term_entry);
        }
        // TODO check all rows have the same number of entries (Is this appropriate?)
        $fileindex++;
      }
    }
  }
  $reportindex = 0;
  if (count($valid_CSV) == 0) {
    if (empty($files)) {
      $errormessage .= 'No file has been uploaded<br/>';
    }
  }
  else {
    foreach ($valid_CSV as $validityflag) {
      if (!$validityflag) {
        $errormessage .= 'Uploaded file: ' . $filename[$reportindex] . ' <br/> - is not valid CSV, or <br/> - does not conform to the archive data model, ';
        if ($invalid_labels) {
          $errormessage .= 'or <br/> - has following invalid labels (i.e. contains non-ascii characters)<br/>';
          foreach ($invalid_labels[$reportindex] as $index => $invlabel) {
            $column = $index + 1;
            $errormessage .= 'Invalid label: found in column: ' . $column . '<br/>'; //+1 as columns are counted from zero
          }
        }

        if ($invalid_vocab_terms) {
          $errormessage .= 'or <br/> - has following non-matched terms<br/>';
          foreach ($invalid_vocab_terms[$reportindex] as $index => $invterm) {
            $column = $index + 1;
            $encoding = mb_detect_encoding($invterm);
            if ($encoding !== 'ASCII') {
              $invterm = '(Term has invalid encoding)';
            }
            $errormessage .= 'Invalid term: "' . $invterm . '" found in column: ' . $column . '<br/>'; //+1 as columns are counted from zero
          }
        }
        if ($invalid_unit_terms) {
          $errormessage .= 'or <br/> - has following non-matched Units<br/>';
          foreach ($invalid_unit_terms[$reportindex] as $index => $unitterm) {
            $numofunits = $unitcount[$reportindex][$index];
            $column = $index + 1; //+1 as columns are counted from zero
            $encoding = mb_detect_encoding($unitterm);
            if ($encoding !== 'ASCII') {
              $unitterm = '(Term has invalid encoding)';
            }
            if ($numofunits > 1) {
              $errormessage .= 'More than one unit found in column: ' . $column . '<br/>';
            }
            $errormessage .= 'Invalid unit: "' . $unitterm . '" found in column: ' . $column . '<br/>';
          }
        }
        if ($invalid_taxon_terms) {
          $errormessage .= 'or <br/> - has highlighted the following non-matched Taxon term(s), which will be created when all other validation passes in this csv file<br/>';
          foreach ($invalid_taxon_terms[$reportindex] as $taxon_col => $taxon_terms) {
            foreach ($taxon_terms as $index => $taxonterm) {
              $column = $taxon_col + 1;
              $encoding = mb_detect_encoding($taxonterm);
              if ($encoding !== 'ASCII') {
                $taxonterm = '(Term has invalid encoding)';
              }
              $errormessage .= '- "' . $taxonterm . '" found in column: ' . $column . '<br/>'; //+1 as columns are counted from zero
            }
          }
        }

        if ($invalid_term_entry) {
          $errormessage .= 'or <br/> - has following general invalid term entry value<br/>';
          foreach ($invalid_term_entry[$reportindex] as $term_col => $invalid_terms) {
            foreach ($invalid_terms as $index => $invalidterm) {
              $column = $term_col + 1;
              $encoding = mb_detect_encoding($taxonterm);
              if ($encoding !== 'ASCII') {
                $invalidterm = '(Term has invalid encoding)';
              }
              $errormessage .= 'Invalid term: "' . $invalidterm . '" found in column: ' . $column . '<br/>'; //+1 as columns are counted from zero
            }
          }
        }
      }
      $reportindex++;
    }

    /**
     * From NBywell code
     * This is a check to determine if an entry already exists
     * in the current user collection.
     */
    $label = $form_state['values']['title'];
    $user_id = $user->islandora_user_pid;
    $user_collection_ns = str_replace(array('-', ':'), '', $user_id);
    $user_collection = $user_collection_ns . ':collection';
    $collection_pid = $form_state['values']['collection_pid'];

    if (!strncmp($collection_pid, 'dtcds:', 5)) { //5 from the length of dtcds
      $sparqlQuery = 'SELECT ?title WHERE { ?object <http://purl.org/dc/elements/1.1/title> ?title; <fedora-model:state> <fedora-model:Active>; <fedora-model:hasModel> ?model; <fedora-rels-ext:isMemberOfCollection> ?collection FILTER( (regex(str(?title), "^' . $label . '$", "i") && sameTerm(?collection, <info:fedora/' . $user_collection . '>)) && !sameTerm(?model, <info:fedora/fba:activity_cmodel>)  && !sameTerm(?model, <info:fedora/fba:dataset_cmodel>) && !sameTerm(?model, <info:fedora/si:conceptCModel>) && !sameTerm(?model, <info:fedora/fedora-system:FedoraObject-3.0>))} LIMIT 10';
      $errormessage .= check_for_duplicate_label($label, $sparqlQuery);
    }
  }
  if ($errormessage) {
    form_set_error('uploaded_file', $errormessage);
    $form_state['form_has_errors'] = TRUE;
//as there's been an error, best to clear the list of uploaded files
    $_SESSION['file_uploads'][$form_state['values']['form_build_id']] = array();
  }
  return;
}

function validate_csv_unit_taxon_line($unit_terms, &$valid_CSV, &$fileindex, &$resultpid, &$invalid_unit_terms, &$invalid_taxon_terms, &$invalid_term_entry) {
//Only one unit is allowed
  foreach ($unit_terms as $key => $unitterm) {
    if (empty($unitterm)) {
      $invalid_term_entry[$resultpid][$key][] = 'Three header rows are mandatory in the csv file';
    }
    else {
      $termarray = explode('~~', $unitterm);
      foreach ($termarray as $termarrayvalue) {
        $colonpos = strpos($termarrayvalue, ':');
        $termprefix = trim(substr($termarrayvalue, 0, $colonpos));
        switch ($termprefix) {
          case 'Units': {
              $termvalue = trim(substr($termarrayvalue, $colonpos + 1));
//N/A is a valid unit
              if (strcmp($termvalue, 'N/A') === 0) {
                $checked_unit_result = 'N/A';
              }
              else {
                $checked_unit_result = _check_vocab_term(&$termvalue);
              }
              if ($checked_unit_result === NULL) {
                $invalid_unit_terms[$resultpid][$key] = $termvalue;
              }
              if ($valid_CSV[$fileindex] === TRUE) {
//only allow TRUE to be changed to FALSE, as any FALSE result should be a fail
                $valid_CSV[$fileindex] = !($invalid_unit_terms[$resultpid]);
              }

              break;
            }
          case 'Taxon': {
              $taxonvalue = trim(substr($termarrayvalue, $colonpos + 1));
              $checked_taxon_result = _check_taxon_term(&$taxonvalue);
              if ($checked_taxon_result === NULL) {
//there can be more than one in here
                $invalid_taxon_terms[$resultpid][$key][] = $taxonvalue;
              }
              else {
                $valid_taxon_terms[$resultpid][$key] = $taxonvalue;
              }
              break;
            }
          default: {
//Anything else is invalid
              $invalid_term_entry[$resultpid][$key][] = $termarrayvalue;
              break;
            }
        }
      }
    }
  }
}

function supp_ingest_form_validate($form, &$form_state, $maxfiles = 1, $filetype = 'image') {

//SFOX ripped this from sidora batch ingest code in ingest.inc
  unset($_SESSION['messages']);

  /* start of code added by NAB */
  if (isset($form['archivalRecordCreationDate']['#value'])) {
    $date_str = sprintf("%s %s", date("Y-m-d"), date("H:i:s"));
    $form['archivalRecordCreationDate']['#value'] = $date_str;
    $form['#post']['archivalRecordCreationDate'] = $date_str; // This is the assignment that actually results in the date string being entered in the form.
    $form_state['values']['archivalRecordCreationDate'] = $date_str;
    $_POST['archivalRecordCreationDate'] = $date_str;
  }
  /* end of code added by NAB */

  $files = $_SESSION['file_uploads'][$form_state['values']['form_build_id']];
  if (!empty($files)) {
    if (count($files) > $maxfiles) {
      $errormessage .= 'A maximum of ' . $maxfiles;
      if ($maxfiles == 1) {
        $errormessage .= ' file ';
      }
      else {
        $errormessage .= ' files ';
      }

      $errormessage .= 'can be uploaded per object<br/>';
    }
  }
  $reportindex = 0;
  if (count($files) == 0) {
    if (empty($files)) {
      $errormessage .= 'No file has been uploaded<br/>';
    }
  }
  else {
    foreach ($files as $validityflag) {
      if (!$validityflag) {
        $errormessage .= 'Uploaded file: ' . $filename[$reportindex] . ' <br/> - is not a valid ' . $filetype . ', or <br/> - does not conform to the archive data model, ';
      }
      $reportindex++;
    }

    module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');
    global $user;
    $duplicateEntryAlertRequired = false;

    /**
     * From NBywell code
     * This is a check to determine if an entry already exists
     * in the current collection for the a particular set of collections.
     * As it is hard-coded it is debatable whether it should be included 
     * in the publically released version of this code on GitHub.
     */
    $label = $form_state['values']['titleInfo']['title'];
    $user_id = $user->islandora_user_pid;
    $user_collection_ns = str_replace(array('-', ':'), '', $user_id);
    $user_collection = $user_collection_ns . ':collection';
    $collection_pid = $form_state['values']['collection_pid'];

// Processing for Fidora. In Fidora, when a data-component
// is processed, the pid of the dataset is present in
// collection_pid, so slightly non-standard processing
// is required to cater for this.
    if (!strncmp($collection_pid, 'dtcds:', 5)) { //5 from the length of dtcds
      $sparqlQuery = 'SELECT ?title WHERE { ?object <http://purl.org/dc/elements/1.1/title> ?title; <fedora-model:state> <fedora-model:Active>; <fedora-model:hasModel> ?model; <fedora-rels-ext:isMemberOfCollection> ?collection FILTER( (regex(str(?title), "^' . $label . '$", "i") && sameTerm(?collection, <info:fedora/' . $user_collection . '>)) && !sameTerm(?model, <info:fedora/fba:activity_cmodel>)  && !sameTerm(?model, <info:fedora/fba:dataset_cmodel>) && !sameTerm(?model, <info:fedora/si:conceptCModel>) && !sameTerm(?model, <info:fedora/fedora-system:FedoraObject-3.0>))} LIMIT 10';
      $linkArray = get_link_array($sparqlQuery);
      if (count($linkArray) > 0) {
        foreach ($linkArray as $linkItem) {
          $existingTitle = $linkItem['title'];
        }
        $duplicateEntryAlertRequired = true;
      }
    }

    if ($duplicateEntryAlertRequired) {
      if (strcmp($label, $existingTitle)) {
        $errormessage = 'The entry "' . $label . '" is already present within this collection as "' . $existingTitle . '", so it has not been created.';
      }
      else {
        $errormessage = 'The entry "' . $label . '" is already present within this collection, so it has not been created.';
      }
    }
  }
  if ($errormessage) {
    form_set_error('uploaded_file', $errormessage);
    $form_state['form_has_errors'] = TRUE;
//as there's been an error, best to clear the list of uploaded files
    $_SESSION['file_uploads'][$form_state['values']['form_build_id']] = array();
  }
  return;
}

function supp_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  module_load_include('inc', 'fedora_repository', 'MimeClass');

  if (!$form_state['form_has_errors']) {
    global $user;

    $form_id = $form_state['values']['form_id'];
    switch ($form_id) {
      case 'content_model_viewer_ingest_resource_metadata_form': {
          //Run the disseminators here?
          //SFOX code taken from fedora_repository_ingest_form_validate($form, $form_state);
          module_load_include('inc', 'fedora_repository', 'MimeClass');

          $files = $_SESSION['file_uploads'][$form_state['values']['form_build_id']];
          $file = $files[0]->filepath;
          $imagepid = $form_state['values']['pid'];
          $contentModelPid = ContentModel::getPidFromIdentifier($form_state['values']['models']);
          $contentModelDsid = ContentModel::getDSIDFromIdentifier($form_state['values']['models']);
          $mimetype = new MimeClass();
          $dformat = $mimetype->getType($file);
          $pidnamespace = fba_solution_pack_get_namespace_from_cmodel($contentModelPid) . ':';
          if (($cm = ContentModel::loadFromModel($contentModelPid, $contentModelDsid, $pidnamespace)) !== FALSE) {
            $allowedMimeTypes = $cm->getMimetypes();

            $mimetype = new MimeClass();
            $dformat = $mimetype->getType($file);

            if (!empty($file)) {
              if (!in_array($dformat, $allowedMimeTypes)) {
                form_set_error($ifl, t('The uploaded file\'s mimetype (@mime) is not associated with this Content Model. The allowed types are: @allowed', array(
                  '@mime' => $dformat,
                  '@allowed' => implode(', ', $allowedMimeTypes),
                )));
                //change to drupal_set_message? above
                file_delete($file);
                return;
              }
              elseif (!$cm->execIngestRules($file, $dformat)) {
                drupal_set_message(t('Error following Content Model Rules'), 'error');
                foreach (ContentModel::$errors as $err) {
                  drupal_set_message(check_plain($err), 'error');
                }
              }
              module_load_include('inc', 'fedora_repository', 'api/fedora_item');
              $item = new Fedora_Item($imagepid);

              if (!empty($_SESSION['fedora_ingest_files'])) {
                foreach ($_SESSION['fedora_ingest_files'] as $dsid => $datastream_file) {
                  $item->add_datastream_from_file($datastream_file, $dsid);
                }
              }
            }
          }
          $pid = $form_state['values']['pid'];

          $repository = tuque_wrapper_get_repository_instance();
          $repo_object = $repository->getObject($pid);

          //add relext to new object, isMemberOfCollection -> private user collection
          $user_id = $user->islandora_user_pid;
          $usercollectionns = str_replace(array('-', ':'), '', $user_id);
          $usercollection = $usercollectionns . ':collection';
          $repo_object->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $usercollection);
          break;
        }
      case 'content_model_viewer_edit_metadata_form': {
          //the object has been edited, not created
          $here = 1;
        }
      default: {
          
        }
    }
  }
}

function resource_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');

  if (!$form_state['form_has_errors']) {
    global $user;

    $form_id = $form_state['values']['form_id'];
    switch ($form_id) {
      case 'content_model_viewer_ingest_resource_metadata_form': {
          submit_uploaded_files($form, $form_state);
          break;
        }
      case 'content_model_viewer_edit_metadata_form': {
          //the object has been edited, not created
        }
      default: {
          
        }
    }
  }
}

/*
 * This function (as opposed to the repair function) doesn't recreate the COLS 
 *  datastreams for existing OBJ datastreams, or redoes the rels int links
 */

function create_datacomponent_datastreams($pid) {
  module_load_include('module', 'tuque_wrapper', 'tuque_wrapper');
  module_load_include('inc', 'sidora', 'includes/fba_mysql');

  $repository = tuque_wrapper_get_repository_instance();
  $repo_object = $repository->getObject($pid);
  $colsds_content = array(); //array of column datastreams for later ingestion
  foreach ($repo_object as $datastream) {
    $dsids[] = $datastream->id;
  }
  foreach ($dsids as $dsid) {
    $dsidprefix = substr($dsid, 0, 3);

    if (strcmp($dsidprefix, 'OBJ') === 0) { //starts with OBJ
      //if the matching COLS datastream exists, then don't recreate it
      //Assumption, if one COLS exists, then the full amount of COLS datastreams do too.
      $dsidnumber = substr($dsid, 3); //just get the datastream number (e.g. ignoring the 'OBJ' bit)
      $colsexist = count(preg_grep('/^COLS' . $dsidnumber . '-/', $dsids));

      if ($colsexist === 0) {
        //The COLS datastreams don't exist, so it's ok to create them
        $objdatastream = $repository->api->a->getDatastreamDissemination($repo_object->id, $dsid, NULL); // Work around for bug in Tuque API: ISLANDORA-752
        // Processing First Row of CSV file (user defined headers)
        // Extract to an array containing each header
        $headers = trim_array_values(explode(",", strtok($objdatastream, "\n")));
        // Processing Second Row of CSV files (vocab terms)
        // Extract to an array containing each vocab_term then convert to DTC vocab pid
        $vocab_terms = trim_array_values(explode(",", strtok("\n")));
        // NB TODO replace the use of explode() with fgetcsv (in case cells have commas in them)

        for ($i = 0, $j = count($vocab_terms); $i < $j; $i++) {
          $term = trim($vocab_terms[$i]);
          // CSV file has already been validated using the check_vocab_term function, so
          // a vocab pid will be returned for each element in vocab_terms
          $vocab_terms_pids[$i] = _check_vocab_term(&$term);
        }

        // Processing Third Row of CSV files (units)
        // Extract the third row information into an array
        // This can contain Unit: and Taxon: and these are delimited by ~~ 
        // (possibly with whitespace before/after)
        $units = trim_array_values(explode(",", strtok("\n")));
        foreach ($units as $key => $unitterm) {
          $termarray = explode('~~', $unitterm);
          $unitcount[$fileindex][$key] = 0;
          foreach ($termarray as $termarrayvalue) {
            $colonpos = strpos($termarrayvalue, ':');
            $termprefix = trim(substr($termarrayvalue, 0, $colonpos));
            switch ($termprefix) {
              case 'Units': {
                  $unitcount[$fileindex][$key] ++;
                  $termvalue = trim(substr($termarrayvalue, $colonpos + 1));
                  //N/A is a valid unit but isn't in the vocabulary
                  if (strcmp($termvalue, 'N/A') === 0) {
                    //$checked_unit_result = 'N/A';
                    $units[$key] = 'N/A';
                  }
                  else {
                    //$checked_unit_result = _check_vocab_term(&$termvalue);
                    $unit = _get_units($termvalue);
                    $units[$key] = _check_vocab_term($unit);
                  }

                  /*
                    if ($checked_unit_result === NULL) {
                    $invalid_unit_term_found[$fileindex] = true;
                    $invalid_unit_terms[$fileindex][$key] = $termvalue;
                    }
                    else {
                    $valid_unit_terms[$fileindex][$key] = $termvalue;
                    } */
                  break;
                }
              case 'Taxon': {
                  $taxonvalue = trim(substr($termarrayvalue, $colonpos + 1));
                  $taxon = _get_taxon($taxonvalue);
                  $checkedtaxonterm = _check_taxon_term($taxon);
                  if (is_null($checkedtaxonterm)) {
                    //Need to create an object for this as it doesn't yet exist
                    $scientificName = $taxonvalue;
                    $namespace = 'fwltaxon';
                    $collection_pid = 'info:fedora/' . $namespace . ':collection';
                    $nextavailablepid = fedora_item::get_next_PID_in_namespace($namespace);
                    global $user;
                    $item = fedora_item::ingest_new_item($nextavailablepid, 'A', $scientificName, $user->name);
                    $item->add_relationship('isMemberOfCollection', $collection_pid, RELS_EXT_URI);
                    $item->add_relationship('hasModel', 'fba:taxon_cmodel', FEDORA_MODEL_URI);
                    $dwcdsid = 'DWC';
                    $UTCdateformatstring = 'Y-m-d\TH:i:s'; // need to append '.\000Z';                                    
                    $reccreDateTime = new DateTime();
                    //Translate to YYYY-MM-DDTHH:MM:SS.uuuZ
                    $reccresolrDateTime = $reccreDateTime->format($UTCdateformatstring) . '.000Z';

                    // PHP tries to be clever if it detects http colon slash slash in a string so it
                    // it necessary to break up the namespacing into segments.
                    $DWC1 = '<DarwinRecordSet xmlns="ht' .
                        'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:dcterms="ht' .
                        'tp://purl.org/dc/terms/" xmlns:dwr="ht' .
                        'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:xsi="ht' .
                        'tp://www.w3.org/2001/XMLSchema-instance" xmlns:dwc="ht' .
                        'tp://rs.tdwg.org/dwc/terms/">';

                    $DWC2 = sprintf('<dwc:Taxon><dwc:taxonID></dwc:taxonID><dwc:parentNameUsage></dwc:parentNameUsage><dwc:parentNameUsageID></dwc:parentNameUsageID><dwc:taxonRank></dwc:taxonRank><dwc:scientificName>%s</dwc:scientificName><dwc:scientificNameAuthorship></dwc:scientificNameAuthorship><dwc:namePublishedIn></dwc:namePublishedIn><dwc:nomenclaturalCode></dwc:nomenclaturalCode><dwc:taxonomicStatus></dwc:taxonomicStatus><dwc:vernacularName></dwc:vernacularName><dwc:acceptedNameUsage></dwc:acceptedNameUsage><dwc:acceptedNameUsageID></dwc:acceptedNameUsageID><dwc:originalNameUsage></dwc:originalNameUsage><dwc:originalNameUsageID></dwc:originalNameUsageID><dwc:taxonRemarks>unverified</dwc:taxonRemarks><dcterms:modified>%s</dcterms:modified></dwc:Taxon></DarwinRecordSet>', $scientificName, $reccresolrDateTime);

                    $DWCDatastream = $DWC1 . $DWC2;

                    //need to convert the date formats from 2013-03-12 to 2013-03-12T00:00:00.000Z

                    $item->add_datastream_from_string($DWCDatastream, $dwcdsid, 'DWC Record', 'text/xml', 'X');

                    $proc = new XsltProcessor();

                    $xsl = new DomDocument();
                    $path = drupal_get_path('module', 'islandora_content_model_forms');
                    $xsl->load($path . '/transforms/dwc_taxon_to_dc.xsl');
                    $input = new DomDocument();
                    $input->loadXML(trim($DWCDatastream));
                    $proc->importStylesheet($xsl);
                    $newdom = $proc->transformToDoc($input);
                    $dc_xml = $newdom->saveXML();
                    $item->add_datastream_from_string($dc_xml, 'DC', 'DC', 'text/xml', 'X');
                    $taxa[$key][] = 'info:fedora/' . $item->pid;
                  }
                  else {
                    $cttxml = $checkedtaxonterm->asXML();
                    $taxa[$key][] = substr($cttxml, strpos($cttxml, '"') + 1, -1);
                  }
                  break;
                }
              default: {
                  break;
                }
            }
          }
          //put some flag in here about valid csv?
        }
        $numberofcolumns = count($headers);
        //Use a mysql temporary table, as memory isn't big enough for huge arrays
        $current_row = explode(",", strtok("\n"));
        //$data_array = new SplFixedArray($arraysize);

        $ret = fba_create_temp_dataset_mysql_table($numberofcolumns, $pid);
        //$data_array = array();
        while (count($current_row) > 1 && $current_row[0] !== NULL) {
          // add new data to the data array
          fba_write_temp_dataset_mysql_table($current_row, $numberofcolumns, $pid);
          //$data_array[$rowcount] = $current_row;
          $current_row = explode(",", strtok("\n"));
        }

        // 5. Generate COLSn-n datastream [see later functions]
        for ($colindex = 0; $colindex < $numberofcolumns; $colindex++) {
          $coldsname = 'COLS' . $dsidnumber . '-' . ($colindex + 1); //+1 as loop starts at 0
          $paramvocabtermpid = $paramunit = $paramtaxa = NULL;
          $paramheader = $headers[$colindex];
          if ($vocab_terms_pids[$colindex]) {
            $paramvocabtermpid = (string) $vocab_terms_pids[$colindex];
          }
          if ($units[$colindex]) {
            $paramunit = (string) $units[$colindex];
          }
          if ($taxa[$colindex]) {
            $paramtaxa = $taxa[$colindex];
          }
          $colsds_content[$coldsname] = _create_column_datastream($colindex, $paramheader, $paramvocabtermpid, $paramunit, $paramtaxa, $pid, $dsid, $numberofcolumns);
        }
        //create the COLSxx-y datastreams and the RELS-INT links for the new OBJxx datastream
        $objsrelations = $repo_object[$dsid]->relationships;
        foreach ($colsds_content as $columnname => $colds) {
          $cols_datastream = _create_datastream($repo_object, $columnname, 'X', 'Objects column for this object', 'application/rdf+xml', $colds->asXML());
          $repo_object->ingestDatastream($cols_datastream);
          $objsrelations->add('info:fedora/fedora-system:def/relations-internal#', 'hasConstituent', $pid . '/' . $columnname, RELS_TYPE_URI);
        }
      }
      unset($colsds_content);
    }
  }
  $ret = fba_drop_temp_dataset_mysql_table($pid);
}

function repair_datacomponent_datastreams($pid) {
  module_load_include('inc', 'sidora', 'includes/fba_mysql');

  $repository = tuque_wrapper_get_repository_instance();
  $repo_object = $repository->getObject($pid);
  //Initialise the RELS-INT datastream, ready for each csv's relations

  $colsds_content = array(); //array of column datastreams for later ingestion
  foreach ($repo_object as $datastream) {
    $dsid = $datastream->id;
    if (substr_compare($dsid, 'OBJ', 0, 3) === 0) { //starts with OBJ
      $objdatastream = $repository->api->a->getDatastreamDissemination($repo_object->id, $dsid, NULL); // Work around for bug in Tuque API: ISLANDORA-752
      // Processing First Row of CSV file (user defined headers)
      // Extract to an array containing each header
      $headers = trim_array_values(explode(",", strtok($objdatastream, "\n")));
      // Processing Second Row of CSV files (vocab terms)
      // Extract to an array containing each vocab_term then convert to DTC vocab pid
      $vocab_termss = trim_array_values(explode(",", strtok("\n")));
      // NB TODO replace the use of explode() with fgetcsv (in case cells have commas in them)

      for ($i = 0, $j = count($vocab_termss); $i < $j; $i++) {
        $term = trim($vocab_termss[$i]);
        // CSV file has already been validated using the check_vocab_term function, so
        // a vocab pid will be returned for each element in vocab_terms
        $vocab_terms_pids[$i] = _check_vocab_term(&$term);
      }

      // Processing Third Row of CSV files (units)
      // Extract the third row information into an array
      // This can contain Unit: and Taxon: and these are delimited by ~~ 
      // (possibly with whitespace before/after)
      $units = trim_array_values(explode(",", strtok("\n")));
      foreach ($units as $key => $unitterm) {
        $termarray = explode('~~', $unitterm);
        $unitcount[$fileindex][$key] = 0;
        foreach ($termarray as $termarrayvalue) {
          $colonpos = strpos($termarrayvalue, ':');
          $termprefix = trim(substr($termarrayvalue, 0, $colonpos));
          switch ($termprefix) {
            case 'Units': {
                $unitcount[$fileindex][$key] ++;
                $termvalue = trim(substr($termarrayvalue, $colonpos + 1));
                //N/A is a valid unit but isn't in the vocabulary
                if (strcmp($termvalue, 'N/A') === 0) {
                  //$checked_unit_result = 'N/A';
                  $units[$key] = 'N/A';
                }
                else {
                  //$checked_unit_result = _check_vocab_term(&$termvalue);
                  $unit = _get_units($termvalue);
                  $units[$key] = _check_vocab_term($unit);
                }

                /*
                  if ($checked_unit_result === NULL) {
                  $invalid_unit_term_found[$fileindex] = true;
                  $invalid_unit_terms[$fileindex][$key] = $termvalue;
                  }
                  else {
                  $valid_unit_terms[$fileindex][$key] = $termvalue;
                  } */
                break;
              }
            case 'Taxon': {
                $taxonvalue = trim(substr($termarrayvalue, $colonpos + 1));
                $taxon = _get_taxon($taxonvalue);
                $checkedtaxonterm = _check_taxon_term($taxon);
                if (is_null($checkedtaxonterm)) {
                  //Need to create an object for this as it doesn't yet exist
                  $scientificName = $taxonvalue;
                  $namespace = 'fwltaxon';
                  $collection_pid = 'info:fedora/' . $namespace . ':collection';
                  $nextavailablepid = fedora_item::get_next_PID_in_namespace($namespace);
                  global $user;
                  $item = fedora_item::ingest_new_item($nextavailablepid, 'A', $scientificName, $user->name);
                  $item->add_relationship('isMemberOfCollection', $collection_pid, RELS_EXT_URI);
                  $item->add_relationship('hasModel', 'fba:taxon_cmodel', FEDORA_MODEL_URI);
                  $dwcdsid = 'DWC';
                  $UTCdateformatstring = 'Y-m-d\TH:i:s'; // need to append '.\000Z';                                    
                  $reccreDateTime = new DateTime();
                  //Translate to YYYY-MM-DDTHH:MM:SS.uuuZ
                  $reccresolrDateTime = $reccreDateTime->format($UTCdateformatstring) . '.000Z';

                  // PHP tries to be clever if it detects http colon slash slash in a string so it
                  // it necessary to break up the namespacing into segments.
                  $DWC1 = '<DarwinRecordSet xmlns="ht' .
                      'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:dcterms="ht' .
                      'tp://purl.org/dc/terms/" xmlns:dwr="ht' .
                      'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:xsi="ht' .
                      'tp://www.w3.org/2001/XMLSchema-instance" xmlns:dwc="ht' .
                      'tp://rs.tdwg.org/dwc/terms/">';

                  $DWC2 = sprintf('<dwc:Taxon><dwc:taxonID></dwc:taxonID><dwc:parentNameUsage></dwc:parentNameUsage><dwc:parentNameUsageID></dwc:parentNameUsageID><dwc:taxonRank></dwc:taxonRank><dwc:scientificName>%s</dwc:scientificName><dwc:scientificNameAuthorship></dwc:scientificNameAuthorship><dwc:namePublishedIn></dwc:namePublishedIn><dwc:nomenclaturalCode></dwc:nomenclaturalCode><dwc:taxonomicStatus></dwc:taxonomicStatus><dwc:vernacularName></dwc:vernacularName><dwc:acceptedNameUsage></dwc:acceptedNameUsage><dwc:acceptedNameUsageID></dwc:acceptedNameUsageID><dwc:originalNameUsage></dwc:originalNameUsage><dwc:originalNameUsageID></dwc:originalNameUsageID><dwc:taxonRemarks>unverified</dwc:taxonRemarks><dcterms:modified>%s</dcterms:modified></dwc:Taxon></DarwinRecordSet>', $scientificName, $reccresolrDateTime);

                  $DWCDatastream = $DWC1 . $DWC2;

                  //need to convert the date formats from 2013-03-12 to 2013-03-12T00:00:00.000Z

                  $item->add_datastream_from_string($DWCDatastream, $dwcdsid, 'DWC Record', 'text/xml', 'X');

                  $proc = new XsltProcessor();

                  $xsl = new DomDocument();
                  $path = drupal_get_path('module', 'islandora_content_model_forms');
                  $xsl->load($path . '/transforms/dwc_taxon_to_dc.xsl');
                  $input = new DomDocument();
                  $input->loadXML(trim($DWCDatastream));
                  $proc->importStylesheet($xsl);
                  $newdom = $proc->transformToDoc($input);
                  $dc_xml = $newdom->saveXML();
                  $item->add_datastream_from_string($dc_xml, 'DC', 'DC', 'text/xml', 'X');
                  $taxa[$key][] = 'info:fedora/' . $item->pid;
                }
                else {
                  $cttxml = $checkedtaxonterm->asXML();
                  $taxa[$key][] = substr($cttxml, strpos($cttxml, '"') + 1, -1);
                }
                break;
              }
            default: {
                break;
              }
          }
        }
        //put some flag in here about valid csv?
      }
      // go through and extract the data into an array of arrays
      $current_row = explode(",", strtok("\n"));
      //$data_array = array();
      $ret = fba_create_temp_dataset_mysql_table($numberofcolumns, $pid);

      while (count($current_row) > 1 && $current_row[0] !== NULL) {
        // add new data to the data array
        //$data_array[] = $current_row;
        fba_write_temp_dataset_mysql_table($current_row, $numberofcolumns, $pid);

        $current_row = explode(",", strtok("\n"));
      };


      // 5. Generate COLSn-n datastream [see later functions]
      $numberofcolumns = count($headers);
      $objnumber = substr($dsid, 3); //just get the datastream number (ignoring the 'OBJ' bit)
      $columnnames = array();
      for ($colindex = 0; $colindex < $numberofcolumns; $colindex++) {
        $coldsname = 'COLS' . $objnumber . '-' . ($colindex + 1); //+1 as loop starts at 0
        $paramvocabtermpid = $paramunit = $paramtaxa = NULL;
        $paramheader = $headers[$colindex];
        if ($vocab_terms_pids[$colindex]) {
          $paramvocabtermpid = (string) $vocab_terms_pids[$colindex];
        }
        if ($units[$colindex]) {
          $paramunit = (string) $units[$colindex];
        }
        if ($taxa[$colindex]) {
          $paramtaxa = $taxa[$colindex];
        }
        $colsds_content[$coldsname] = _create_column_datastream($colindex, $paramheader, $paramvocabtermpid, $paramunit, $paramtaxa, $pid, $dsid, $numberofcolumns);
        $columnnames[] = $coldsname; //for the rels ints below
      }
      // 6. Generate RELS-INT datastream [see later functions]
      $relsintds_header = new SimpleXMLElement('<rdf:RDF 
                xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
                xmlns:rdfs="http://www.w3.org/2000/01/rdf-schema#" 
                xmlns:relsext="info:fedora/fedora-system:def/relations-external#" 
                xmlns:relsint="info:fedora/fedora-system:def/relations-internal#" 
                xmlns:altrelsext="http://www.fedora.info/definitions/1/0/fedora-relsext-ontology.rdfs#" 
                xmlns:fba="http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/" 
                xmlns:fedora-model="info:fedora/fedora-system:def/model#"
                xmlns:dc="http://purl.org/dc/terms/"/>', NULL, FALSE, "http://www.w3.org/1999/02/22-rdf-syntax-ns#", TRUE);

      $relsintds_content = _create_RELS($relsintds_header, $pid, $dsid, $columnnames);
    }
  }

  //create (or update) the COLS datastreams
  foreach ($colsds_content as $columnname => $colds) {
    $cols_datastream = _create_datastream(
        $repo_object, $columnname, 'X', 'Fedora Internal Relationships file for this object', 'application/rdf+xml', $colds->asXML());
    $repo_object->ingestDatastream($cols_datastream);
  }
  //
  //need to add this to existing RELS-INT datastream, or add if first time around.
  $relsintxml = $relsintds_content->asXML();
  $rels_int_datastream = _create_datastream($repo_object, 'RELS-INT', 'X', 'Fedora Internal Relationships file for this object', 'application/rdf+xml', $relsintxml);
  $repo_object->ingestDatastream($rels_int_datastream);
  $ret = fba_drop_temp_dataset_mysql_table($pid);
}

function concept_form_submit($form, &$form_state) {
  global $user;

  switch ($form_state['values']['form_id']) {
    case 'content_model_viewer_ingest_concept_metadata_form': {
        $pid = $form_state['values']['pid'];
        $repository = tuque_wrapper_get_repository_instance();
        $new_dataset_object = $repository->getObject($pid);
        //add relext to new object, isMemberOfCollection -> private user collection
        $user_id = $user->islandora_user_pid;
        $usercollectionns = str_replace(array('-', ':'), '', $user_id);
        $usercollection = $usercollectionns . ':collection';
        $new_dataset_object->relationships->add(RELS_EXT_URI, 'isMemberOfCollection', $usercollection);
        break;
      }
    case 'content_model_viewer_edit_metadata_form': {
        //the object has been edited, not created
        break;
      }
    default: {
        
      }
  }
}

function concept_ingest_form_validate($form, &$form_state) {
  /* start of code added by NAB */
  if (isset($form['archivalRecordCreationDate']['#value'])) {
    $date_str = sprintf("%s %s", date("Y-m-d"), date("H:i:s"));
    $form['archivalRecordCreationDate']['#value'] = $date_str;
    $form['#post']['archivalRecordCreationDate'] = $date_str; // This is the assignment that actually results in the date string being entered in the form.
    $form_state['values']['archivalRecordCreationDate'] = $date_str;
    $_POST['archivalRecordCreationDate'] = $date_str;
  }
  /* end of code added by NAB */
}
