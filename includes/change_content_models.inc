<?php

/**
 * @file
 * ChangeContentModels.inc
 */
function islandora_change_content_models_form($form, &$form_state, $collection_object, $url) {
  $collection_pid = $collection_object->id;
  $content_models = get_content_models_list($collection_object);
  $cm_options = array();
  $name_mappings = array();
  foreach ($content_models as $content_model) {
    if ($content_model != "islandora:collectionCModel") {
      $item = $collection_object->repository->getObject($content_model);
      $cm_name = $item->Label;
      $cm_options[$content_model] = $cm_name;
    }
  }

  $namespace = drupal_substr($collection_pid, 0, strpos($collection_pid, ":"));

  $collection_policy_dsid = variable_get('Islandora_Collection_Policy_DSID', 'COLLECTION_POLICY');
  $collection_policy_datastream = $collection_object->getDatastream($collection_policy_dsid);

  $supported_collection_models = array();
  if ($collection_policy_datastream && $collection_policy_datastream->content) {
    $collection_policy = new CollectionPolicy($collection_policy_datastream->content);
    $supported_collection_models = $collection_policy->getContentModels();
  }
  $collection_namespace = drupal_substr($collection_pid, 0, strpos($collection_pid, ":"));

  $collection_name = $collection_object->label;
  $new_content_models = get_content_models_as_option_array();
  $current_models_in_policy = array();

  if ($supported_collection_models) {
    foreach ($supported_collection_models as $supported_model) {
      $current_models_in_policy[$supported_model['pid']] = $supported_model['pid'];
    }
  }

  $form['link'] = array(
    '#type' => 'item',
    '#markup' => l(t("Return to Collection Manager"), $url),
  );

  $form['page_title'] = array(
    '#type' => 'item',
    '#markup' => '<h1>' . t('Change Content Models') . '</h1>',
  );

  $form['change_cmodel']['titlebox'] = array(
    '#type' => 'item',
    '#title' => t("Change content models within @collection_name", array('@collection_name' => $collection_object->label)),
  );

  $form['change_cmodel']['current_content_model'] = array(
    '#title' => "Choose content model to be changed",
    '#type' => 'select',
    '#options' => $current_models_in_policy,
    '#description' => t("All objects in this collection with the selected content model will be changed."),
  );

  $form['change_cmodel']['new_content_model'] = array(
    '#title' => "Choose new content model",
    '#type' => 'select',
    '#options' => $new_content_models,
    '#description' => t("The new content model to be assigned to selected objects."),
  );

  $form['change_cmodel']['collection_pid'] = array(
    '#type' => 'hidden',
    '#value' => $collection_pid,
  );

  $form['change_cmodel']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Change content model associations'),
    '#id' => 'change_model',
  );

  return $form;
}

function islandora_change_content_models_form_validate($form, &$form_state) {

}

function islandora_change_content_models_form_submit($form, &$form_state) {
  $current_content_model = $form_state['values']['current_content_model'];
  $new_content_model = $form_state['values']['new_content_model'];
  $collection_pid = $form_state['values']['collection_pid'];

  $current_content_model_object = islandora_object_load($current_content_model);
  $collection_object = islandora_object_load($form_state['values']['collection_pid']);

  $collection_policy_datastream = $collection_object->getDatastream(variable_get('Islandora_Collection_Policy_DSID', 'COLLECTION_POLICY'));
  $policy = new CollectionPolicy($collection_policy_datastream->content);

  $collection_policy_xml = new DOMDocument();
  $collection_policy_xml->loadXML($collection_policy_datastream->content);

  $add_to_policy = TRUE;
  $policy_content_models = $policy->getContentModels();
  foreach ($policy_content_models as $policy_content_model) {
    if ($policy_content_model['pid'] == $current_content_model) {
      $namespace = $policy_content_model['namespace'];
    }
    if ($policy_content_model['pid'] == $new_content_model) {
      $add_to_policy = FALSE;
    }
  }
  if ($add_to_policy) {
    $new_content_model_object = islandora_object_load($new_content_model);
    $new_content_model_datastream = $new_content_model_object->getDatastream(variable_get('Islandora_Content_Model_DSID', 'ISLANDORACM'));
    $content_models_element = $collection_policy_xml->getElementsByTagName('content_models');
    $content_model_element = $content_models_element->item(0)->getElementsByTagName('content_model');

    $content_model_element = $collection_policy_xml->createElement('content_model');
    $content_model_element->setAttribute('name', $new_content_model_object->label);
    $content_model_element->setAttribute('dsid', variable_get('Islandora_Content_Model_DSID', 'ISLANDORACM'));
    $content_model_element->setAttribute('namespace', $namespace);
    $content_model_element->setAttribute('pid', $new_content_model_object->id);
    $content_models_element->item(0)->appendChild($content_model_element);

    $new_collection_policy_datastream = $collection_object->getDatastream('COLLECTION_POLICY');
    $new_collection_policy_datastream->setContentFromString($collection_policy_xml->saveXML());
    $new_collection_policy_datastream->label = 'COLLECTION_POLICY';
    $collection_object->ingestDatastream($new_collection_policy_datastream);
  }

  $query = "select \$object  from <#ri>
    where (\$object <info:fedora/fedora-system:def/model#hasModel> <info:fedora/$current_content_model>
      and (\$object <info:fedora/fedora-system:def/relations-external#isMemberOfCollection> <info:fedora/$collection_pid>
           or \$object <info:fedora/fedora-system:def/relations-external#isMemberOf> <info:fedora/$collection_pid>)
      and \$object <fedora-model:state> <info:fedora/fedora-system:def/model#Active>)";

  $objects = $collection_object->repository->ri->itqlQuery($query, 'unlimited');

  $count = 0;
  foreach ($objects as $object) {
    if (!$object['object']['value']) {
      continue;
    }
    $fedora_item = islandora_object_load($object['object']['value']);
    $fedora_item->relationships->remove(FEDORA_MODEL_URI, 'hasModel', $current_content_model);
    $fedora_item->relationships->add(FEDORA_MODEL_URI, 'hasModel', $new_content_model);
    $count++;
  }
  drupal_set_message(t('@current_content_model changed to @new_content_model on @count objects', array('@current_content_model' => $current_content_model, '@new_content_model' => $new_content_model, '@count' => $count)));
}
