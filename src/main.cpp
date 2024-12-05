#include <iostream>
#include <pthread.h>
#include <string.h>
#include <queue>
#include <map>
#include <fstream>
#include <algorithm>
using namespace std;

// structura pt argumentele functiei de mapperi
struct mapper_args {
    queue<pair<string, int>>* files;
    pthread_mutex_t* mutex;
    pthread_barrier_t* barrier;
    vector<map<string, int>>* partial_list;
};

// structura pt argumentele functiei de reduceri
struct reducer_args {
    vector<map<string, int>>* partial_list;
    pthread_mutex_t* mutex;
    pthread_barrier_t* barrier;
    vector<pair<string, vector<int>>>* final_list;
    queue<char>* alphabet;
};

bool map_cmp(const pair<string, vector<int>>& a, const pair<string, vector<int>>& b)
{
    // se sorteaza perechile {cuvant, {id_file}} descrescator
    // in functie de numarul de aparitii a cuvantului in fisiere
    // si apoi crescator alfabetic
    if (a.second.size() == b.second.size()) {
        return a.first < b.first;
    }

    return a.second.size() > b.second.size();
}

// creaza coada de fisiere
queue<pair<string, int>> create_queue(string file)
{
    // deschide fisierul
    ifstream main_file(file);
    if (!main_file.is_open()) {
        cerr << "Could not open the main file." << endl;
        exit(1);
    }

    // se creaza o coada de fisiere
    queue<pair<string, int>> files;

    int nr_files;
    // citim numarul de fisiere
    main_file >> nr_files;

    string filename;
    for (int i = 0; i < nr_files; i++) {
        // citim numele fisierelor si le adaugam in coada
        // cu tot cu id-ul lor
        main_file >> filename;
        files.push({filename, i + 1});
    }
    
    // inchidem fisierul
    main_file.close();

    return files;
}

// functia de mapperi
void* mappers_func(void* args)
{
    // se extrag argumentele
    mapper_args* arguments = (mapper_args*)args;
    queue<pair<string, int>>* files = arguments->files;
    pthread_mutex_t* mutex = arguments->mutex;
    pthread_barrier_t* barrier = arguments->barrier;
    vector<map<string, int>>* partial_list = arguments->partial_list;

    // cat timp mai sunt fisiere de procesat
    // --blochez thread-ul in bucla daca mai are de lucru --threadpool
    while (true) {
        pthread_mutex_lock(mutex);

        // daca nu mai sunt fisiere de procesat ne oprim
        if (files->empty()) {
            pthread_mutex_unlock(mutex);
            break;
        }

        // se extrage un fisier din coada
        pair<string, int> file;
        if (!files->empty()) {
            file = files->front();
            files->pop();
            pthread_mutex_unlock(mutex);
        }

        // se deschide fisierul extras din coada
        ifstream input_file(file.first);
        if (!input_file.is_open()) {
            cerr << "Could not open the " << file.first << " file." << endl;
            exit(1);
        }

        string word;
        map<string, int> aux;
        // se citeste continutul fisierului --cuvant cu cuvant
        while (input_file >> word) {
            // se transforma cuvantul in lowercase
            for (size_t i = 0; i < word.size(); i++) {
                word[i] = tolower(word[i]);
            }

            // se verifica daca cuvantul are caractere speciale
            // --daca da, se elimina
            for (size_t i = 0; i < word.size(); i++) {
                if (!isalpha(word[i])) {
                    word.erase(i, 1);
                    i--;
                }
            }

            // se adauga cuvantul in map doar daca acesta nu este deja adaugat
            if (aux.find(word) == aux.end()) {
                aux[word] = file.second;
            }
        }

        // adaugam fisierul prelucrat in lista partiala
        pthread_mutex_lock(mutex);
        (*partial_list).push_back(aux);
        pthread_mutex_unlock(mutex);

        // se inchide fisierul
        input_file.close();
    }

    // asteptam ca toate thread-urile sa isi termine treaba
    pthread_barrier_wait(barrier);

    pthread_exit(nullptr);
}

queue<char> create_alphabet()
{
    queue<char> alphabet;
    for (char i = 'a'; i <= 'z'; i++) {
        alphabet.push(i);
    }

    return alphabet;
}

void* reducers_func(void *args)
{
    // se extrag argumentele
    struct reducer_args *arguments = (reducer_args*)args;
    vector<map<string, int>> *partial_list = arguments->partial_list;
    pthread_mutex_t *mutex = arguments->mutex;
    pthread_barrier_t *barrier = arguments->barrier;
    vector<pair<string, vector<int>>> *final_list = arguments->final_list;
    queue<char> *alphabet = arguments->alphabet;

    // se astapta ca toate thread-urile de mappari sa isi termine treaba
    pthread_barrier_wait(barrier);

    // blocam thread-ul in bucla daca mai are de lucru --threadpool
    while (true) {
        pthread_mutex_lock(mutex);

        // daca nu mai sunt map-uri de procesat ne oprim
        if (alphabet->empty()) {
            // asteptam toate thread-urile sa isi termine treaba
            // inainte sa iesim din bucla
            pthread_mutex_unlock(mutex);
            break;
        }

        char letter;
        // se extrage o litera din alfabet
        if (!alphabet->empty()) {
            letter = alphabet->front();
            alphabet->pop();
            pthread_mutex_unlock(mutex);
        }

        map<string, vector<int>> aux;
        // se cauta cuvintele din map-urile partiale care incep cu litera curenta
        for (size_t i = 0; i < partial_list->size(); i++) {
            for (auto map = (*partial_list)[i].begin(); map != (*partial_list)[i].end(); map++) {
                // daca cuvantul incepe cu litera curenta
                if (map->first[0] == letter) {
                    // daca cuvantul nu este in map-ul auxiliar, il adaugam
                    if (aux.find(map->first) == aux.end()) {
                        aux[map->first] = {map->second};
                    } else {
                        // daca cuvantul este deja in map-ul auxiliar, adaugam fisierul
                        aux[map->first].push_back(map->second);
                    }
                }
            }
        }

        // se sorteaza map-ul auxiliar
        vector<pair<string, vector<int>>> map_to_vector(aux.begin(), aux.end());
        sort(map_to_vector.begin(), map_to_vector.end(), map_cmp);

        // se sorteaza si vectorul de fisiere a fiecarui cuvant
        for (auto &pair : map_to_vector) {
            sort(pair.second.begin(), pair.second.end());
        }

        // se adauga sortarea in lista finala a reducerilor
        pthread_mutex_lock(mutex);
        for (size_t i = 0; i < map_to_vector.size(); i++) {
            (*final_list).push_back(map_to_vector[i]);
        }
        pthread_mutex_unlock(mutex);
    }

    pthread_exit(nullptr);
}

int main(int argc, char **argv)
{
    // verifica ca numarul de argumente sa fie corect
    if (argc < 4) {
        cerr << "Invalid number of arguments." << endl;
        exit(1);
    }

    // extrage argumentele
    int nr_mappers = atoi(argv[1]);
    int nr_reducers = atoi(argv[2]);
    string file = argv[3];

    // se creaza coada de fisiere
    queue<pair<string, int>> files = create_queue(file);

    // se creaza structura de argumente pentru functia de mapperi
    struct mapper_args m_args;
    m_args.files = &files;

    pthread_mutex_t m_mutex;
    pthread_mutex_init(&m_mutex, nullptr);
    m_args.mutex = &m_mutex;

    pthread_barrier_t m_barrier;
    pthread_barrier_init(&m_barrier, nullptr, nr_mappers + nr_reducers);
    m_args.barrier = &m_barrier;

    vector <map<string, int>> partial_list;
    m_args.partial_list = &partial_list;

    // se creaza structura de argumente pentru functia de reduceri
    struct reducer_args r_args;
    r_args.partial_list = &partial_list;

    pthread_mutex_t r_mutex;
    pthread_mutex_init(&r_mutex, nullptr);
    r_args.mutex = &r_mutex;

    r_args.barrier = &m_barrier;

    vector<pair<string, vector<int>>> final_list;
    r_args.final_list = &final_list;

    queue<char> alphabet = create_alphabet();
    r_args.alphabet = &alphabet;

    // se creaza thread-urile de mapperi si reduceri
    pthread_t threads[nr_mappers + nr_reducers];
    int k = 0;
    while (k < nr_mappers + nr_reducers) {
        // se creaza thread-urile de mapperi
        if (k < nr_mappers) {
            int ret = pthread_create(&threads[k], nullptr, mappers_func, (void*)&m_args);

            // se verifica daca thread-ul a fost creat cu succes
            if (ret) {
                cerr << "Error creating thread no. " << k << "." << endl;
                exit(1);
            }

            k++;
        // se creaza thread-urile de reduceri
        } else {
            int ret = pthread_create(&threads[k], nullptr, reducers_func, (void*)&r_args);

            // se verifica daca thread-ul a fost creat cu succes
            if (ret) {
                cerr << "Error creating thread no. " << k << "." << endl;
                exit(1);
            }

            k++;
        }
    }

    // se da join la toate thread-urile
    for (int i = 0; i < nr_mappers + nr_reducers; i++) {
        int ret = pthread_join(threads[i], nullptr);

        // se verifica daca thread-ul a fost asteptat cu succes
        if (ret) {
            cerr << "Error joining thread no. " << i << "." << endl;
            exit(1);
        }
    }

    // se distruge mutex-ul
    pthread_mutex_destroy(&m_mutex);
    pthread_mutex_destroy(&r_mutex);

    // se distruge bariera
    pthread_barrier_destroy(&m_barrier);

    // se scrie in fisierul de output
    for (char letter = 'a'; letter <= 'z'; letter++) {
        string filename;

        // se deschide fisierul corespunzator literei
        filename = string(1, letter) + ".txt";
        ofstream letter_file(filename);
        if (!letter_file.is_open()) {
            cerr << "Could not open the " << filename << " file." << endl;
            exit(1);
        }

        // se scrie in fisierul corespunzator literei
        for (size_t i = 0; i < final_list.size(); i++) {
            if (final_list[i].first[0] == letter) {
                letter_file << final_list[i].first << ":[";
                for (size_t j = 0; j < final_list[i].second.size(); j++) {
                    if (j != final_list[i].second.size() - 1) {
                        letter_file << final_list[i].second[j] << " ";
                    } else {
                        letter_file << final_list[i].second[j] << "]";
                    }
                }

                letter_file << endl;
            }
        }

        // se inchide fisierul
        letter_file.close();
    }

    return 0;
}
