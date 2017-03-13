<?php

function fba_create_temp_dataset_mysql_table($numcols, $pid) {
  $cleanedpid = str_replace(':', '', $pid);
  $tablename = 'fba_temp_dataset_' . $cleanedpid;

  $ret = array();

  if (!db_table_exists($tablename)) {

    for ($i = 0; $i < $numcols; $i++) {
      $colname = 'COL' . ($i + 1);
      $fields[$colname] = array(
        'type' => 'text',
        'not null' => TRUE,
      );
    }
    $schema[$tablename] = array(
      'description' => t('Temporary table to allow columns to be created in batches.'),
      'fields' => $fields,
    );
    $ret = db_create_table($tablename, $schema[$tablename]);
  }

  return $ret;
}

function fba_drop_temp_dataset_mysql_table($pid) {
  $cleanedpid = str_replace(':', '', $pid);
  $dbname = 'fba_temp_dataset_' . $cleanedpid;
  $ret = db_drop_table($dbname);
  return $ret;
}

function fba_write_temp_dataset_mysql_table($row, $numofcols, $pid) {
  $cleanedpid = str_replace(':', '', $pid);
  $tablename = 'fba_temp_dataset_' . $cleanedpid;
  for ($i = 0; $i < $numofcols; $i++) {
    $querycolumns[] = 'COL' . ($i + 1);
  }
  foreach ($row as $rowvalue) {
    $queryvalues[] = $rowvalue;
  }
  $result = db_insert($tablename)->fields($querycolumns)->values($queryvalues)->execute();
}
